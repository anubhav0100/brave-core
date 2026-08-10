// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/delegation/delegation_process_manager.h"

#include <windows.h>

#include <utility>

#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/strcat_win.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "brave/browser/ui/sidebar/sidebar_service_factory.h"
#include "brave/components/sidebar/browser/sidebar_item.h"
#include "brave/components/sidebar/browser/sidebar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kDelegationRepoUrl[] =
    "https://github.com/anubhav0100/the-delegation.git";

// Fixed rather than finding a free port, same rationale as n8n's kN8nPort -
// the sidebar item and tools can build URLs without a round trip.
constexpr int kDelegationPort = 3210;

constexpr base::TimeDelta kInitialHealthCheckDelay = base::Seconds(1);
constexpr base::TimeDelta kHealthCheckInterval = base::Seconds(1);
constexpr int kMaxHealthCheckAttempts = 30;

constexpr size_t kMaxOutputBufferBytes = 512 * 1024;
constexpr size_t kReadChunkSize = 4096;

// Cold-start budget for each one-time setup step - see the design doc's
// "first-run cost" section. `git clone --depth 1` of this repo is small;
// `npm install`/`npm run build` are the slow ones (a Three.js/WebGPU/React
// app's dependency tree), but were both well under a minute in local
// testing - these ceilings are generous headroom for a slower machine or
// network, not the expected case.
constexpr base::TimeDelta kCloneTimeout = base::Minutes(2);
constexpr base::TimeDelta kInstallTimeout = base::Minutes(5);
constexpr base::TimeDelta kBuildTimeout = base::Minutes(3);

// static
base::FilePath GetDataDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &user_data_dir);
  return user_data_dir.Append(FILE_PATH_LITERAL("BraveDelegationData"));
}

// Runs on its own background sequence for as long as `read_handle` stays
// open - identical in shape to n8n_process_manager.cc's ReadPipeLoop, see
// its comment for why blocking reads are fine here.
void ReadPipeLoop(base::win::ScopedHandle read_handle,
                  base::RepeatingCallback<void(std::string)> on_chunk,
                  base::OnceClosure on_closed) {
  char buffer[kReadChunkSize];
  for (;;) {
    DWORD bytes_read = 0;
    BOOL ok = ::ReadFile(read_handle.get(), buffer, sizeof(buffer),
                        &bytes_read, nullptr);
    if (!ok || bytes_read == 0) {
      break;
    }
    on_chunk.Run(std::string(buffer, bytes_read));
  }
  std::move(on_closed).Run();
}

// Runs one command to completion on the current (background) sequence,
// capturing combined stdout+stderr and blocking until it exits or
// `timeout` elapses. `program`/`args` are passed as a command-line string
// (not argv) since GetAppOutputWithExitCodeAndTimeout requires that on
// Windows; `cmd.exe /c` wrapping is needed for npm/npx, which are .cmd
// batch wrapper scripts that can't be exec'd directly - same constraint
// n8n_process_manager.cc's LaunchProcessAndReport documents.
struct StepResult {
  bool success = false;
  std::string output;
};

StepResult RunSetupStep(const std::wstring& command_line,
                        const base::FilePath& cwd,
                        base::TimeDelta timeout) {
  base::LaunchOptions options;
  options.start_hidden = true;
  options.current_directory = cwd;

  std::string output;
  int exit_code = -1;
  bool completed = base::GetAppOutputWithExitCodeAndTimeout(
      command_line, /*include_stderr=*/true, &output, &exit_code, timeout,
      options);
  return {completed && exit_code == 0, std::move(output)};
}

// Runs entirely on a background sequence. Performs whichever one-time
// setup steps are still needed - clone if `.git` is missing, npm install
// if `node_modules` is missing, npm run build if `dist/index.html` is
// missing - stopping at the first failure. Each step is skipped (not
// re-run) if its output already exists on disk, so a second browser
// session's EnsureStarted() call is fast (straight to launching the
// server) rather than re-cloning/reinstalling/rebuilding every time - see
// the design doc's "first-run cost" section. An explicit "Update"
// re-clone/rebuild is a possible follow-up, not built here.
StepResult RunSetupPipelineOnBackgroundSequence(base::FilePath data_dir) {
  std::string combined_output;

  if (!base::PathExists(data_dir.Append(FILE_PATH_LITERAL(".git")))) {
    combined_output += "[setup] Cloning The Delegation...\r\n";
    base::FilePath parent_dir = data_dir.DirName();
    base::CreateDirectory(parent_dir);
    std::wstring clone_cmd = base::StrCat(
        {L"git.exe clone --depth 1 ",
         base::UTF8ToWide(kDelegationRepoUrl), L" \"", data_dir.value(),
         L"\""});
    StepResult result = RunSetupStep(clone_cmd, parent_dir, kCloneTimeout);
    combined_output += result.output;
    if (!result.success) {
      return {false, combined_output +
                         "\r\n[setup] git clone failed - is Git installed "
                         "and on PATH?\r\n"};
    }
  }

  if (!base::PathExists(data_dir.Append(FILE_PATH_LITERAL("node_modules")))) {
    combined_output += "\r\n[setup] Installing dependencies (npm install)...\r\n";
    StepResult result = RunSetupStep(L"cmd.exe /c npm install", data_dir,
                                     kInstallTimeout);
    combined_output += result.output;
    if (!result.success) {
      return {false, combined_output + "\r\n[setup] npm install failed.\r\n"};
    }
  }

  if (!base::PathExists(
          data_dir.Append(FILE_PATH_LITERAL("dist"))
              .Append(FILE_PATH_LITERAL("index.html")))) {
    combined_output += "\r\n[setup] Building (npm run build)...\r\n";
    StepResult result =
        RunSetupStep(L"cmd.exe /c npm run build", data_dir, kBuildTimeout);
    combined_output += result.output;
    if (!result.success) {
      return {false, combined_output + "\r\n[setup] npm run build failed.\r\n"};
    }
  }

  combined_output += "\r\n[setup] Done.\r\n";
  return {true, combined_output};
}

net::NetworkTrafficAnnotationTag GetHealthCheckTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation(
      "delegation_process_health_check", R"(
      semantics {
        sender: "Delegation Process Manager"
        description:
          "Polls the local Delegation instance the browser itself "
          "launched, to find out when its web server has actually "
          "started answering requests."
        trigger: "The user or AI Assistant asks to start Delegation."
        data: "No request body. Contacts localhost only."
        destination: LOCAL
        internal {
          contacts {
            email: "ai-chat@brave.com"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-08-10"
      }
      policy {
        cookies_allowed: NO
        setting: "This feature cannot be disabled independently of AI Chat."
        policy_exception_justification:
          "Only ever talks to the localhost Delegation instance the "
          "browser itself started."
      })");
}

net::NetworkTrafficAnnotationTag GetApiTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("delegation_api_call", R"(
      semantics {
        sender: "Delegation Process Manager"
        description:
          "Reads simulation status from, or sends an approve/reject/inject "
          "control action to, the local Delegation instance the browser "
          "itself launched."
        trigger: "The AI Assistant checks or controls the simulation."
        data: "Simulation status (tasks/agents) or a control action; "
              "localhost only."
        destination: LOCAL
        internal {
          contacts {
            email: "ai-chat@brave.com"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-08-10"
      }
      policy {
        cookies_allowed: NO
        setting: "This feature cannot be disabled independently of AI Chat."
        policy_exception_justification:
          "Only ever talks to the localhost Delegation instance the "
          "browser itself started."
      })");
}

}  // namespace

DelegationProcessManager::DelegationProcessManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  EnsureSidebarItemRegistered();
}

DelegationProcessManager::~DelegationProcessManager() {
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

bool DelegationProcessManager::IsRunning() const {
  return process_.IsValid();
}

void DelegationProcessManager::EnsureSidebarItemRegistered() {
  Profile* profile = Profile::FromBrowserContext(browser_context_);
  if (!profile) {
    return;
  }
  auto* sidebar_service =
      sidebar::SidebarServiceFactory::GetForProfile(profile);
  if (!sidebar_service) {
    return;
  }
  GURL delegation_url(base::StrCat(
      {"http://localhost:", base::NumberToString(kDelegationPort),
       "/the-delegation/"}));
  for (const auto& item : sidebar_service->items()) {
    if (item.url == delegation_url) {
      return;  // Already registered - a web item's URL is its id.
    }
  }
  sidebar_service->AddItem(sidebar::SidebarItem::Create(
      delegation_url, u"Delegation", sidebar::SidebarItem::Type::kTypeWeb,
      sidebar::SidebarItem::BuiltInItemType::kNone,
      /*open_in_panel=*/true));
}

void DelegationProcessManager::EnsureStarted(StartedCallback callback) {
  if (is_ready_) {
    std::move(callback).Run(true);
    return;
  }
  pending_started_callbacks_.push_back(std::move(callback));
  if (IsRunning()) {
    return;  // A launch/health-check poll is already in flight.
  }
  if (setup_started_) {
    return;  // Setup pipeline already running on the background sequence.
  }
  setup_started_ = true;
  AppendOutput("[setup] Checking whether Delegation needs to be set up...\r\n");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RunSetupPipelineOnBackgroundSequence, GetDataDir()),
      base::BindOnce(
          [](base::WeakPtr<DelegationProcessManager> self,
             StepResult result) {
            if (self) {
              self->OnSetupPipelineComplete(result.success,
                                            std::move(result.output));
            }
          },
          weak_ptr_factory_.GetWeakPtr()));
}

void DelegationProcessManager::OnSetupPipelineComplete(
    bool success,
    std::string combined_output) {
  setup_started_ = false;
  AppendOutput(combined_output);
  if (!success) {
    ResolvePendingStartedCallbacks(false);
    return;
  }
  LaunchServerAndReport();
}

void DelegationProcessManager::LaunchServerAndReport() {
  base::CommandLine cmd(base::FilePath(FILE_PATH_LITERAL("node.exe")));
  cmd.AppendArgPath(GetDataDir()
                        .Append(FILE_PATH_LITERAL("server"))
                        .Append(FILE_PATH_LITERAL("delegation-server.mjs")));
  cmd.AppendArg(base::StrCat({"--port=", base::NumberToString(kDelegationPort)}));

  // Hidden-console, pipe-captured output - identical pattern to
  // n8n_process_manager.cc's LaunchProcessAndReport; see its comment for
  // why both start_hidden and the pipe redirection are needed together.
  SECURITY_ATTRIBUTES pipe_sa = {};
  pipe_sa.nLength = sizeof(pipe_sa);
  pipe_sa.bInheritHandle = TRUE;

  HANDLE read_raw = nullptr;
  HANDLE write_raw = nullptr;
  if (!::CreatePipe(&read_raw, &write_raw, &pipe_sa, 0)) {
    ResolvePendingStartedCallbacks(false);
    return;
  }
  base::win::ScopedHandle read_handle(read_raw);
  base::win::ScopedHandle write_handle(write_raw);
  ::SetHandleInformation(read_handle.get(), HANDLE_FLAG_INHERIT, 0);

  base::win::ScopedHandle stdin_handle(::CreateFileW(
      L"NUL", GENERIC_READ, FILE_SHARE_READ, &pipe_sa, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!stdin_handle.is_valid()) {
    ResolvePendingStartedCallbacks(false);
    return;
  }

  base::LaunchOptions options;
  options.start_hidden = true;
  options.current_directory = GetDataDir();
  options.stdin_handle = stdin_handle.get();
  options.stdout_handle = write_handle.get();
  options.stderr_handle = write_handle.get();
  options.inherit_mode = base::LaunchOptions::Inherit::kSpecific;
  options.handles_to_inherit = {stdin_handle.get(), write_handle.get()};

  process_ = base::LaunchProcess(cmd, options);
  if (!process_.IsValid()) {
    ResolvePendingStartedCallbacks(false);
    return;
  }

  write_handle.Close();
  stdin_handle.Close();

  scoped_refptr<base::SequencedTaskRunner> reader_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT});
  reader_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ReadPipeLoop, std::move(read_handle),
          base::BindPostTaskToCurrentDefault(
              base::BindRepeating(&DelegationProcessManager::AppendOutput,
                                  weak_ptr_factory_.GetWeakPtr())),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              [](base::WeakPtr<DelegationProcessManager> self) {
                if (!self) {
                  return;
                }
                for (auto& observer : self->observers_) {
                  observer.OnDelegationRunningStateChanged(false);
                }
              },
              weak_ptr_factory_.GetWeakPtr()))));

  port_ = kDelegationPort;
  base_url_ = base::StrCat({"http://localhost:", base::NumberToString(port_)});

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DelegationProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), kMaxHealthCheckAttempts),
      kInitialHealthCheckDelay);
}

void DelegationProcessManager::PollForReady(int attempts_remaining) {
  if (!IsRunning()) {
    ResolvePendingStartedCallbacks(false);
    return;
  }
  if (attempts_remaining <= 0) {
    ResolvePendingStartedCallbacks(false);
    return;
  }
  if (!health_check_helper_) {
    auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    health_check_helper_ =
        std::make_unique<api_request_helper::APIRequestHelper>(
            GetHealthCheckTrafficAnnotationTag(), std::move(url_loader_factory));
  }
  health_check_helper_->Request(
      "GET", GURL(base::StrCat({base_url_, "/api/health"})), "", "",
      base::BindOnce(&DelegationProcessManager::OnHealthCheckResponse,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining));
}

void DelegationProcessManager::OnHealthCheckResponse(
    int attempts_remaining,
    api_request_helper::APIRequestResult result) {
  if (result.response_code() > 0) {
    is_ready_ = true;
    for (auto& observer : observers_) {
      observer.OnDelegationRunningStateChanged(true);
    }
    ResolvePendingStartedCallbacks(true);
    return;
  }
  if (!IsRunning()) {
    ResolvePendingStartedCallbacks(false);
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DelegationProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining - 1),
      kHealthCheckInterval);
}

void DelegationProcessManager::ResolvePendingStartedCallbacks(bool success) {
  std::vector<StartedCallback> callbacks =
      std::move(pending_started_callbacks_);
  pending_started_callbacks_.clear();
  for (auto& callback : callbacks) {
    std::move(callback).Run(success);
  }
}

void DelegationProcessManager::GetState(GetStateCallback callback) {
  if (!IsReady()) {
    // Deliberately not calling EnsureStarted() - a status check shouldn't
    // launch the process as a side effect, same rule as
    // N8nProcessManager::ListMcpWorkflows.
    std::move(callback).Run(false, base::DictValue());
    return;
  }
  if (!api_helper_) {
    auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    api_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
        GetApiTrafficAnnotationTag(), std::move(url_loader_factory));
  }
  api_helper_->Request(
      "GET", GURL(base::StrCat({base_url_, "/api/state"})), "", "",
      base::BindOnce(&DelegationProcessManager::OnStateResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DelegationProcessManager::OnStateResponse(
    GetStateCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode() || !result.value_body().is_dict()) {
    std::move(callback).Run(false, base::DictValue());
    return;
  }
  std::move(callback).Run(true, result.value_body().GetDict().Clone());
}

void DelegationProcessManager::SendControlAction(const std::string& action,
                                                  base::DictValue payload,
                                                  SendControlCallback callback) {
  if (!IsReady()) {
    std::move(callback).Run(
        false, "Delegation isn't running - open it first.");
    return;
  }
  base::DictValue body;
  body.Set("action", action);
  body.Set("payload", std::move(payload));
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  if (!api_helper_) {
    auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    api_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
        GetApiTrafficAnnotationTag(), std::move(url_loader_factory));
  }
  api_helper_->Request(
      "POST", GURL(base::StrCat({base_url_, "/api/control"})), body_json,
      "application/json",
      base::BindOnce(&DelegationProcessManager::OnControlResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DelegationProcessManager::OnControlResponse(
    SendControlCallback callback,
    api_request_helper::APIRequestResult result) {
  if (result.Is2XXResponseCode()) {
    std::move(callback).Run(true, "");
    return;
  }
  std::move(callback).Run(
      false, base::StrCat({"Delegation returned ",
                           base::NumberToString(result.response_code()),
                           " for the control action."}));
}

void DelegationProcessManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void DelegationProcessManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void DelegationProcessManager::AppendOutput(std::string chunk) {
  output_buffer_.append(chunk);
  if (output_buffer_.size() > kMaxOutputBufferBytes) {
    size_t excess = output_buffer_.size() - kMaxOutputBufferBytes;
    size_t newline_pos = output_buffer_.find('\n', excess);
    output_buffer_.erase(0, newline_pos == std::string::npos
                                ? excess
                                : newline_pos + 1);
  }
  for (auto& observer : observers_) {
    observer.OnDelegationOutputAppended(chunk);
  }
}

void DelegationProcessManager::Shutdown() {
  health_check_helper_.reset();
  api_helper_.reset();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

}  // namespace ai_chat

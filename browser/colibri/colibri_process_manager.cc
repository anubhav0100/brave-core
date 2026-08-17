// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/colibri/colibri_process_manager.h"

#include <windows.h>

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

// Colibri's OpenAI-compatible API defaults to this address (see
// docs/api.md's `coli serve --host 127.0.0.1 --port 8000`) - fixed rather
// than negotiated, same rationale as N8nProcessManager's fixed port.
constexpr char kColibriHost[] = "127.0.0.1";
constexpr char kColibriPort[] = "8000";

// Generous compared to N8nProcessManager/DelegationProcessManager's ~30s
// budget: colibri's own docs describe minutes-long cold loads for its
// larger (300GB+, disk-streamed) models before the API answers anything.
constexpr base::TimeDelta kInitialHealthCheckDelay = base::Seconds(2);
constexpr base::TimeDelta kHealthCheckInterval = base::Seconds(2);
constexpr int kMaxHealthCheckAttempts = 300;  // ~10 minutes

constexpr size_t kMaxOutputBufferBytes = 512 * 1024;
constexpr size_t kReadChunkSize = 4096;

// Runs on its own background sequence for as long as `read_handle` stays
// open - identical in shape to n8n_process_manager.cc/
// delegation_process_manager.cc's ReadPipeLoop.
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

net::NetworkTrafficAnnotationTag GetHealthCheckTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation(
      "colibri_process_health_check", R"(
      semantics {
        sender: "Colibri Process Manager"
        description:
          "Polls the local Colibri instance the browser itself launched, "
          "to find out when its OpenAI-compatible API has actually "
          "started answering requests."
        trigger: "The user starts Colibri from Leo Assistant settings."
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
        last_reviewed: "2026-08-18"
      }
      policy {
        cookies_allowed: NO
        setting: "This feature cannot be disabled independently of AI Chat."
        policy_exception_justification:
          "Only ever talks to the localhost Colibri instance the browser "
          "itself started."
      })");
}

}  // namespace

ColibriProcessManager::ColibriProcessManager(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ColibriProcessManager::~ColibriProcessManager() {
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

bool ColibriProcessManager::IsRunning() const {
  return process_.IsValid();
}

void ColibriProcessManager::EnsureStarted(const base::FilePath& colibri_dir,
                                          const base::FilePath& model_dir,
                                          StartedCallback callback) {
  if (is_ready_) {
    std::move(callback).Run(true);
    return;
  }
  pending_started_callbacks_.push_back(std::move(callback));
  if (IsRunning()) {
    return;  // A launch/health-check poll is already in flight.
  }
  LaunchProcessAndReport(colibri_dir, model_dir);
}

void ColibriProcessManager::LaunchProcessAndReport(
    const base::FilePath& colibri_dir,
    const base::FilePath& model_dir) {
  if (colibri_dir.empty() || model_dir.empty()) {
    AppendOutput(
        "[colibri] Set both the Colibri folder and model folder paths "
        "before starting.\r\n");
    ResolvePendingStartedCallbacks(false);
    return;
  }

  base::CommandLine cmd(base::FilePath(FILE_PATH_LITERAL("python.exe")));
  cmd.AppendArgPath(colibri_dir.Append(FILE_PATH_LITERAL("coli")));
  cmd.AppendArg("serve");
  cmd.AppendArg("--host");
  cmd.AppendArg(kColibriHost);
  cmd.AppendArg("--port");
  cmd.AppendArg(kColibriPort);
  cmd.AppendArg("--model");
  cmd.AppendArgPath(model_dir);

  // Hidden-console, pipe-captured output - identical pattern to
  // n8n_process_manager.cc/delegation_process_manager.cc's
  // LaunchProcessAndReport; see their comments for why both start_hidden
  // and the pipe redirection are needed together.
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
  options.current_directory = colibri_dir;
  options.stdin_handle = stdin_handle.get();
  options.stdout_handle = write_handle.get();
  options.stderr_handle = write_handle.get();
  options.inherit_mode = base::LaunchOptions::Inherit::kSpecific;
  options.handles_to_inherit = {stdin_handle.get(), write_handle.get()};

  process_ = base::LaunchProcess(cmd, options);
  if (!process_.IsValid()) {
    AppendOutput(
        "[colibri] Failed to launch - is Python 3 installed and on "
        "PATH?\r\n");
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
              base::BindRepeating(&ColibriProcessManager::AppendOutput,
                                  weak_ptr_factory_.GetWeakPtr())),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              [](base::WeakPtr<ColibriProcessManager> self) {
                if (!self) {
                  return;
                }
                for (auto& observer : self->observers_) {
                  observer.OnColibriRunningStateChanged(false);
                }
              },
              weak_ptr_factory_.GetWeakPtr()))));

  base_url_ =
      base::StrCat({"http://", kColibriHost, ":", kColibriPort});

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ColibriProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), kMaxHealthCheckAttempts),
      kInitialHealthCheckDelay);
}

void ColibriProcessManager::PollForReady(int attempts_remaining) {
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
      "GET", GURL(base::StrCat({base_url_, "/v1/models"})), "", "",
      base::BindOnce(&ColibriProcessManager::OnHealthCheckResponse,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining));
}

void ColibriProcessManager::OnHealthCheckResponse(
    int attempts_remaining,
    api_request_helper::APIRequestResult result) {
  if (result.response_code() > 0) {
    is_ready_ = true;
    for (auto& observer : observers_) {
      observer.OnColibriRunningStateChanged(true);
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
      base::BindOnce(&ColibriProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining - 1),
      kHealthCheckInterval);
}

void ColibriProcessManager::ResolvePendingStartedCallbacks(bool success) {
  std::vector<StartedCallback> callbacks =
      std::move(pending_started_callbacks_);
  pending_started_callbacks_.clear();
  for (auto& callback : callbacks) {
    std::move(callback).Run(success);
  }
}

void ColibriProcessManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ColibriProcessManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ColibriProcessManager::AppendOutput(std::string chunk) {
  output_buffer_.append(chunk);
  if (output_buffer_.size() > kMaxOutputBufferBytes) {
    size_t excess = output_buffer_.size() - kMaxOutputBufferBytes;
    size_t newline_pos = output_buffer_.find('\n', excess);
    output_buffer_.erase(0, newline_pos == std::string::npos
                                ? excess
                                : newline_pos + 1);
  }
  for (auto& observer : observers_) {
    observer.OnColibriOutputAppended(chunk);
  }
}

void ColibriProcessManager::Shutdown() {
  health_check_helper_.reset();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

}  // namespace ai_chat

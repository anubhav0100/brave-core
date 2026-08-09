// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/n8n/n8n_process_manager.h"

#include <windows.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "third_party/zlib/google/zip.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kN8nApiKeyPref[] = "brave.n8n.api_key";

// n8n's own default port - kept fixed rather than finding a free port so
// the side panel and REST-API tools can build URLs without a round trip to
// ask the process manager first.
constexpr int kN8nPort = 5678;

// n8n takes anywhere from a couple seconds (already-cached npx package,
// warm SQLite migrations) to well over a minute (first-ever run: npx has
// to resolve and download n8n and its dependencies from the npm registry
// before n8n itself even starts booting) to actually start answering HTTP
// requests. EnsureStarted() polls for real readiness instead of trusting
// a fixed delay - see its header comment for why that matters.
constexpr base::TimeDelta kInitialHealthCheckDelay = base::Seconds(2);
constexpr base::TimeDelta kHealthCheckInterval = base::Seconds(1);
constexpr int kMaxHealthCheckAttempts = 90;

constexpr size_t kMaxBackupsToKeep = 7;
constexpr char kBackupFilePrefix[] = "n8n_backup_";
constexpr char kBackupFileExtension[] = ".zip";

// Captured n8n stdout/stderr is trimmed to roughly this many bytes (at a
// newline boundary) so a long-running n8n instance can't grow the buffer
// without bound - see AppendOutput().
constexpr size_t kMaxOutputBufferBytes = 512 * 1024;
constexpr size_t kReadChunkSize = 4096;

// Runs on its own background sequence for as long as `read_handle` stays
// open - which is exactly as long as n8n's process (holding the other,
// inherited copy of the pipe's write end) is alive. Blocking ReadFile()
// calls are fine here; this sequence has nothing else to do. `on_chunk`/
// `on_closed` are expected to already be wrapped with
// base::BindPostTaskToCurrentDefault() by the caller, so calling them here
// actually hops back to N8nProcessManager's own sequence.
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

bool DirectoryHasAnyEntries(const base::FilePath& dir) {
  if (!base::DirectoryExists(dir)) {
    return false;
  }
  base::FileEnumerator enumerator(
      dir, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  return !enumerator.Next().empty();
}

// Runs on a background sequence. Zips `data_dir` into a new timestamped
// file under `backup_dir`, then deletes the oldest backups beyond
// kMaxBackupsToKeep. This is a fire-and-forget periodic task, so failures
// are not reported back to the caller.
void PerformBackupOnBackgroundSequence(base::FilePath data_dir,
                                       base::FilePath backup_dir) {
  if (!DirectoryHasAnyEntries(data_dir)) {
    return;  // Nothing to back up yet - not an error.
  }
  if (!base::CreateDirectory(backup_dir)) {
    return;
  }
  std::string timestamp = base::NumberToString(
      base::Time::Now().InMillisecondsSinceUnixEpoch());
  base::FilePath backup_file = backup_dir.AppendASCII(
      base::StrCat({kBackupFilePrefix, timestamp, kBackupFileExtension}));
  if (!zip::Zip(data_dir, backup_file, /*include_hidden_files=*/true)) {
    return;
  }

  std::vector<base::FilePath> existing_backups;
  base::FileEnumerator enumerator(backup_dir, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (path.BaseName().AsUTF8Unsafe().find(kBackupFilePrefix) == 0) {
      existing_backups.push_back(path);
    }
  }
  // Filenames are millisecond timestamps, so lexicographic sort is also
  // chronological.
  std::sort(existing_backups.begin(), existing_backups.end());
  while (existing_backups.size() > kMaxBackupsToKeep) {
    base::DeleteFile(existing_backups.front());
    existing_backups.erase(existing_backups.begin());
  }
}

// Runs on a background sequence. Returns the most recent backup file if
// `data_dir` is missing/empty and at least one backup exists, else nullopt.
std::optional<base::FilePath> FindBackupToRestoreOnBackgroundSequence(
    base::FilePath data_dir,
    base::FilePath backup_dir) {
  if (DirectoryHasAnyEntries(data_dir)) {
    return std::nullopt;  // Real data already present - never overwrite it.
  }
  if (!base::DirectoryExists(backup_dir)) {
    return std::nullopt;
  }
  std::optional<base::FilePath> most_recent;
  base::FileEnumerator enumerator(backup_dir, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (path.BaseName().AsUTF8Unsafe().find(kBackupFilePrefix) != 0) {
      continue;
    }
    if (!most_recent || path.BaseName().AsUTF8Unsafe() >
                            most_recent->BaseName().AsUTF8Unsafe()) {
      most_recent = path;
    }
  }
  return most_recent;
}

// Runs on a background sequence.
bool RestoreBackupOnBackgroundSequence(base::FilePath backup_file,
                                       base::FilePath data_dir) {
  if (!base::CreateDirectory(data_dir)) {
    return false;
  }
  return zip::Unzip(backup_file, data_dir);
}

net::NetworkTrafficAnnotationTag GetHealthCheckTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("n8n_process_health_check", R"(
      semantics {
        sender: "n8n Process Manager"
        description:
          "Polls the local n8n instance the browser itself launched, to "
          "find out when its web server has actually started answering "
          "requests, rather than assuming it's ready after a fixed delay."
        trigger: "The user or AI Assistant asks to start n8n."
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
          "Only ever talks to the localhost n8n instance the browser "
          "itself started."
      })");
}

}  // namespace

// static
void N8nProcessManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kN8nApiKeyPref, std::string());
}

// static
void N8nProcessManager::SetApiKey(PrefService* prefs,
                                  const std::string& api_key) {
  prefs->SetString(kN8nApiKeyPref, api_key);
}

// static
std::string N8nProcessManager::GetApiKey(PrefService* prefs) {
  return prefs->GetString(kN8nApiKeyPref);
}

// static
base::FilePath N8nProcessManager::GetDataDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &user_data_dir);
  return user_data_dir.Append(FILE_PATH_LITERAL("BraveN8nData"));
}

// static
base::FilePath N8nProcessManager::GetBackupDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &user_data_dir);
  return user_data_dir.Append(FILE_PATH_LITERAL("BraveN8nBackups"));
}

N8nProcessManager::N8nProcessManager(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  // Runs roughly once a day for as long as the browser process is alive -
  // there's no reliable way to run this while the browser is fully closed
  // without a separately-installed Windows Scheduled Task, which is a
  // bigger, separately-scoped feature (see the design doc).
  backup_timer_.Start(FROM_HERE, base::Days(1),
                      base::BindRepeating(&N8nProcessManager::PerformBackup,
                                          weak_ptr_factory_.GetWeakPtr()));
}

N8nProcessManager::~N8nProcessManager() {
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

bool N8nProcessManager::IsRunning() const {
  return process_.IsValid();
}

void N8nProcessManager::PerformBackup() {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&PerformBackupOnBackgroundSequence, GetDataDir(),
                     GetBackupDir()));
}

void N8nProcessManager::MaybeRestoreFromBackup(base::OnceClosure done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&FindBackupToRestoreOnBackgroundSequence, GetDataDir(),
                     GetBackupDir()),
      base::BindOnce(&N8nProcessManager::OnRestoreCheckComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(done)));
}

void N8nProcessManager::OnRestoreCheckComplete(
    base::OnceClosure done,
    std::optional<base::FilePath> backup_to_restore) {
  if (!backup_to_restore) {
    std::move(done).Run();
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&RestoreBackupOnBackgroundSequence, *backup_to_restore,
                     GetDataDir()),
      base::BindOnce(&N8nProcessManager::OnRestoreComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(done)));
}

void N8nProcessManager::OnRestoreComplete(base::OnceClosure done,
                                          bool success) {
  // Whether or not the restore succeeded, proceed to start n8n normally -
  // a failed restore just means it starts fresh, same as if no backup had
  // existed at all. Not treated as a fatal error.
  std::move(done).Run();
}

void N8nProcessManager::EnsureStarted(StartedCallback callback) {
  if (is_ready_) {
    std::move(callback).Run(true);
    return;
  }
  // Either nothing has been launched yet, or a launch/health-check poll is
  // already in flight from an earlier caller - either way, queue this
  // caller to be notified once that settles, rather than launching a
  // second n8n process racing for the same port.
  pending_started_callbacks_.push_back(std::move(callback));
  if (IsRunning()) {
    return;
  }
  if (!restore_checked_) {
    restore_checked_ = true;
    MaybeRestoreFromBackup(base::BindOnce(
        &N8nProcessManager::LaunchProcessAndReport,
        weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  LaunchProcessAndReport();
}

void N8nProcessManager::LaunchProcessAndReport() {
  // `npx` is a .cmd wrapper on Windows and can't be launched directly via
  // CreateProcess - route through cmd.exe /c the same way pnpm/npm's own
  // scripts do in this project's build tooling.
  base::CommandLine cmd(base::FilePath(FILE_PATH_LITERAL("cmd.exe")));
  cmd.AppendArg("/c");
  cmd.AppendArg("npx");
  cmd.AppendArg("--yes");
  cmd.AppendArg("n8n");
  cmd.AppendArg("start");

  auto env = base::Environment::Create();
  env->SetVar("N8N_PORT", base::NumberToString(kN8nPort));
  env->SetVar("N8N_USER_FOLDER", GetDataDir().AsUTF8Unsafe());
  // Skip n8n's own first-run task-completion/diagnostics survey prompts -
  // this is being driven by the browser, not a human clicking through
  // n8n's own onboarding.
  env->SetVar("N8N_DIAGNOSTICS_ENABLED", "false");
  env->SetVar("N8N_VERSION_NOTIFICATIONS_ENABLED", "false");

  // Redirect n8n's stdout/stderr into a pipe this process reads from, and
  // hide its console window entirely (start_hidden). Without both of
  // these, `cmd /c npx ...` pops up a real, visible console window
  // showing the raw command line and every log line n8n prints - exactly
  // the kind of implementation detail meant to stay inside the browser's
  // own "n8n" Settings page instead of a bare OS window. See
  // ReadPipeLoop()/AppendOutput() for where the captured bytes go.
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
  // Only the write end should cross into the child - the read end is ours
  // alone, or the pipe would never signal EOF once n8n exits.
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

  // The child now holds its own inherited duplicates of these handles -
  // close ours so ReadPipeLoop()'s ReadFile() actually returns 0/fails
  // once n8n exits, instead of blocking forever on a write end we're
  // still holding open ourselves.
  write_handle.Close();
  stdin_handle.Close();

  // Deliberately not notifying OnN8nRunningStateChanged(true) here - the
  // OS process existing doesn't mean n8n is actually reachable yet (see
  // PollForReady()). Observers are told once health-checking confirms
  // that, matching what IsReady()/EnsureStarted() consider "ready" too.

  scoped_refptr<base::SequencedTaskRunner> reader_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT});
  reader_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ReadPipeLoop, std::move(read_handle),
          base::BindPostTaskToCurrentDefault(
              base::BindRepeating(&N8nProcessManager::AppendOutput,
                                  weak_ptr_factory_.GetWeakPtr())),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              [](base::WeakPtr<N8nProcessManager> self) {
                if (!self) {
                  return;
                }
                for (auto& observer : self->observers_) {
                  observer.OnN8nRunningStateChanged(false);
                }
              },
              weak_ptr_factory_.GetWeakPtr()))));

  port_ = kN8nPort;
  base_url_ = base::StrCat({"http://localhost:", base::NumberToString(port_)});

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&N8nProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), kMaxHealthCheckAttempts),
      kInitialHealthCheckDelay);
}

void N8nProcessManager::PollForReady(int attempts_remaining) {
  if (!IsRunning()) {
    // Process died before ever becoming reachable - nothing to poll.
    ResolvePendingStartedCallbacks(false);
    return;
  }
  if (attempts_remaining <= 0) {
    // Gave up waiting. The process is left running - check
    // GetBufferedOutput() (or the Settings "n8n" terminal view) for why;
    // a cold `npx` install on a slow connection can still be in progress
    // past this point, in which case a later EnsureStarted() call will
    // resume polling and can still succeed.
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
      "GET", GURL(base_url_), "", "",
      base::BindOnce(&N8nProcessManager::OnHealthCheckResponse,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining));
}

void N8nProcessManager::OnHealthCheckResponse(
    int attempts_remaining,
    api_request_helper::APIRequestResult result) {
  // Any real HTTP response - even a non-2xx one - means n8n's web server
  // is actually up and accepting connections, which is all "ready" needs
  // to mean here. A response code of 0 means the connection itself
  // failed (server not listening yet), so keep polling.
  if (result.response_code() > 0) {
    is_ready_ = true;
    for (auto& observer : observers_) {
      observer.OnN8nRunningStateChanged(true);
    }
    ResolvePendingStartedCallbacks(true);
    return;
  }
  if (!IsRunning()) {
    // Exited while we were mid-request.
    ResolvePendingStartedCallbacks(false);
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&N8nProcessManager::PollForReady,
                     weak_ptr_factory_.GetWeakPtr(), attempts_remaining - 1),
      kHealthCheckInterval);
}

void N8nProcessManager::ResolvePendingStartedCallbacks(bool success) {
  std::vector<StartedCallback> callbacks =
      std::move(pending_started_callbacks_);
  pending_started_callbacks_.clear();
  for (auto& callback : callbacks) {
    std::move(callback).Run(success);
  }
}

void N8nProcessManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void N8nProcessManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void N8nProcessManager::AppendOutput(std::string chunk) {
  output_buffer_.append(chunk);
  if (output_buffer_.size() > kMaxOutputBufferBytes) {
    size_t excess = output_buffer_.size() - kMaxOutputBufferBytes;
    size_t newline_pos = output_buffer_.find('\n', excess);
    output_buffer_.erase(0, newline_pos == std::string::npos
                                ? excess
                                : newline_pos + 1);
  }
  for (auto& observer : observers_) {
    observer.OnN8nOutputAppended(chunk);
  }
}

void N8nProcessManager::Shutdown() {
  backup_timer_.Stop();
  // Releases the browser-context-derived URL loader factory before the
  // context itself tears down.
  health_check_helper_.reset();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

}  // namespace ai_chat

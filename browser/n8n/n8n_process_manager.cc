// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/n8n/n8n_process_manager.h"

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
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "third_party/zlib/google/zip.h"

namespace ai_chat {

namespace {

constexpr char kN8nApiKeyPref[] = "brave.n8n.api_key";

// n8n's own default port - kept fixed rather than finding a free port so
// the side panel and REST-API tools can build URLs without a round trip to
// ask the process manager first.
constexpr int kN8nPort = 5678;

// n8n takes a few seconds to boot its web server after the process starts
// (dependency loading, its own SQLite migrations, etc.) - see the header
// comment on EnsureStarted for why this is a fixed delay rather than a real
// health-check poll.
constexpr base::TimeDelta kStartupGracePeriod = base::Seconds(5);

constexpr size_t kMaxBackupsToKeep = 7;
constexpr char kBackupFilePrefix[] = "n8n_backup_";
constexpr char kBackupFileExtension[] = ".zip";

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

N8nProcessManager::N8nProcessManager() {
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
  if (IsRunning()) {
    std::move(callback).Run(true);
    return;
  }
  if (!restore_checked_) {
    restore_checked_ = true;
    MaybeRestoreFromBackup(
        base::BindOnce(&N8nProcessManager::LaunchProcessAndReport,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
    return;
  }
  LaunchProcessAndReport(std::move(callback));
}

void N8nProcessManager::LaunchProcessAndReport(StartedCallback callback) {
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

  base::LaunchOptions options;
  process_ = base::LaunchProcess(cmd, options);
  if (!process_.IsValid()) {
    std::move(callback).Run(false);
    return;
  }

  port_ = kN8nPort;
  base_url_ = base::StrCat({"http://localhost:", base::NumberToString(port_)});

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<N8nProcessManager> self,
             StartedCallback callback) {
            if (!self) {
              std::move(callback).Run(false);
              return;
            }
            std::move(callback).Run(self->IsRunning());
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      kStartupGracePeriod);
}

void N8nProcessManager::Shutdown() {
  backup_timer_.Stop();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

}  // namespace ai_chat

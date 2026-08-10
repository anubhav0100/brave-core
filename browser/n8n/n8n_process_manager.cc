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
#include "base/containers/flat_map.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/win/scoped_handle.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "brave/browser/ui/sidebar/sidebar_service_factory.h"
#include "brave/components/sidebar/browser/sidebar_item.h"
#include "brave/components/sidebar/browser/sidebar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "third_party/zlib/google/zip.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kN8nApiKeyPref[] = "brave.n8n.api_key";

// List of n8n workflow ids the user has explicitly disconnected from the
// AI Assistant - see SetMcpWorkflowEnabled(). Anything not in this list is
// enabled by default once discovered.
constexpr char kN8nDisabledMcpWorkflowsPref[] =
    "brave.n8n.disabled_mcp_workflows";

constexpr char kMcpTriggerNodeType[] = "@n8n/n8n-nodes-langchain.mcpTrigger";

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

// Name of the Windows Scheduled Task that backs up GetDataDir() even when
// the browser isn't running - see EnsureBackupScheduledTaskRegistered().
constexpr char kBackupScheduledTaskName[] = "BraveN8nDailyBackup";

// Workflow ids come from n8n's own API responses (normally short
// alphanumeric ids), but are used directly as a filesystem directory
// name for version snapshots - sanitize defensively so a surprising id
// can't escape GetWorkflowVersionsDir() or contain path separators.
std::string SanitizeForFileName(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (char c : value) {
    result.push_back((base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) ||
                      c == '-' || c == '_')
                          ? c
                          : '_');
  }
  return result.empty() ? "unknown" : result;
}

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

// Runs on a background sequence. Returns the most recent backup file
// under `backup_dir`, if any - unlike
// FindBackupToRestoreOnBackgroundSequence, doesn't care about
// GetDataDir()'s state, since this is for exporting rather than the
// auto-restore-on-fresh-install path.
std::optional<base::FilePath> FindMostRecentBackupOnBackgroundSequence(
    base::FilePath backup_dir) {
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

// Runs on a background sequence. Ensures at least one backup exists
// (backing up right now if not), then copies the most recent one to
// `destination` - see ExportLatestBackupToFile().
std::pair<bool, std::string> ExportLatestBackupOnBackgroundSequence(
    base::FilePath data_dir,
    base::FilePath backup_dir,
    base::FilePath destination) {
  std::optional<base::FilePath> latest =
      FindMostRecentBackupOnBackgroundSequence(backup_dir);
  if (!latest) {
    PerformBackupOnBackgroundSequence(data_dir, backup_dir);
    latest = FindMostRecentBackupOnBackgroundSequence(backup_dir);
  }
  if (!latest) {
    return {false, "No n8n data to back up yet - open n8n and create a "
                   "flow first."};
  }
  if (!base::CopyFile(*latest, destination)) {
    return {false, "Failed to copy the backup file to the chosen "
                   "location."};
  }
  return {true, base::StrCat({"Exported to ", destination.AsUTF8Unsafe(),
                              " - copy this file to the other machine and "
                              "import it there."})};
}

// Runs on a background sequence. See ImportBackupFromFile().
std::pair<bool, std::string> ImportBackupOnBackgroundSequence(
    base::FilePath backup_zip_path,
    base::FilePath data_dir) {
  if (!base::PathExists(backup_zip_path)) {
    return {false, "That backup file doesn't exist."};
  }
  if (!base::CreateDirectory(data_dir)) {
    return {false, "Couldn't create the n8n data directory."};
  }
  if (!zip::Unzip(backup_zip_path, data_dir)) {
    return {false, "Failed to extract the backup - it may be corrupted "
                   "or not a real n8n backup file."};
  }
  return {true, "Imported successfully - start n8n to use the imported "
                "flows."};
}

// Builds the PowerShell command the scheduled task runs: zips `data_dir`
// into a fresh timestamped file under `backup_dir` (computing the
// timestamp itself, at run time - the whole point of a scheduled task is
// that this runs long after registration) and prunes old backups beyond
// kMaxBackupsToKeep, mirroring PerformBackupOnBackgroundSequence's
// behavior without depending on any brave-core code at all - this needs
// to work even if the browser was uninstalled. AppendArg() (used to send
// this to schtasks.exe below) treats non-ASCII as UTF-8 and quotes it
// correctly, so plain UTF-8 std::string is fine here despite paths being
// native-encoding on Windows.
std::string BuildBackupPowerShellCommand(const base::FilePath& data_dir,
                                         const base::FilePath& backup_dir) {
  std::string data_dir_utf8 = data_dir.AsUTF8Unsafe();
  std::string backup_dir_utf8 = backup_dir.AsUTF8Unsafe();
  return base::StrCat(
      {"$ts=(Get-Date).ToString('yyyyMMddHHmmss'); ",
       "New-Item -ItemType Directory -Force -Path '", backup_dir_utf8,
       "' | Out-Null; ", "if (Test-Path '", data_dir_utf8, "') { ",
       "Compress-Archive -Path '", data_dir_utf8,
       "\\*' -DestinationPath (Join-Path '", backup_dir_utf8,
       "' \"n8n_backup_$ts.zip\") -Force }; ", "Get-ChildItem '",
       backup_dir_utf8,
       "' -Filter 'n8n_backup_*.zip' -ErrorAction SilentlyContinue | ",
       "Sort-Object Name -Descending | Select-Object -Skip ",
       base::NumberToString(kMaxBackupsToKeep), " | Remove-Item -Force"});
}

// Runs on a background sequence. Registers (or overwrites, via /F) a
// Windows Scheduled Task that runs BuildBackupPowerShellCommand() daily -
// see EnsureBackupScheduledTaskRegistered(). Fire-and-forget: nothing
// reads the result, this is best-effort on top of the in-browser daily
// timer.
void RegisterBackupScheduledTaskOnBackgroundSequence(
    base::FilePath data_dir,
    base::FilePath backup_dir) {
  std::string inner_command =
      BuildBackupPowerShellCommand(data_dir, backup_dir);
  std::string task_run_command = base::StrCat(
      {"powershell.exe -NoProfile -WindowStyle Hidden -Command \"",
       inner_command, "\""});

  base::CommandLine cmd(base::FilePath(FILE_PATH_LITERAL("schtasks.exe")));
  cmd.AppendArg("/Create");
  cmd.AppendArg("/TN");
  cmd.AppendArg(kBackupScheduledTaskName);
  cmd.AppendArg("/TR");
  cmd.AppendArg(task_run_command);
  cmd.AppendArg("/SC");
  cmd.AppendArg("DAILY");
  cmd.AppendArg("/ST");
  cmd.AppendArg("03:30");
  cmd.AppendArg("/F");

  base::LaunchOptions options;
  options.start_hidden = true;
  options.wait = true;
  base::LaunchProcess(cmd, options);
}

// Runs on a background sequence.
base::FilePath WorkflowVersionsSubdirForOnBackgroundSequence(
    const base::FilePath& versions_dir,
    const std::string& workflow_id) {
  return versions_dir.AppendASCII(SanitizeForFileName(workflow_id));
}

// Runs on a background sequence.
bool SaveWorkflowVersionSnapshotOnBackgroundSequence(
    base::FilePath versions_dir,
    std::string workflow_id,
    std::string workflow_json) {
  base::FilePath dir =
      WorkflowVersionsSubdirForOnBackgroundSequence(versions_dir, workflow_id);
  if (!base::CreateDirectory(dir)) {
    return false;
  }
  std::string timestamp =
      base::NumberToString(base::Time::Now().InMillisecondsSinceUnixEpoch());
  base::FilePath file = dir.AppendASCII(base::StrCat({timestamp, ".json"}));
  return base::WriteFile(file, workflow_json);
}

// Runs on a background sequence.
std::vector<std::string> ListWorkflowVersionsOnBackgroundSequence(
    base::FilePath versions_dir,
    std::string workflow_id) {
  std::vector<std::string> timestamps;
  base::FilePath dir =
      WorkflowVersionsSubdirForOnBackgroundSequence(versions_dir, workflow_id);
  if (!base::DirectoryExists(dir)) {
    return timestamps;
  }
  base::FileEnumerator enumerator(dir, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (path.Extension() == FILE_PATH_LITERAL(".json")) {
      timestamps.push_back(path.BaseName().RemoveExtension().AsUTF8Unsafe());
    }
  }
  std::ranges::sort(timestamps);
  return timestamps;
}

// Runs on a background sequence.
std::optional<std::string> ReadWorkflowVersionSnapshotOnBackgroundSequence(
    base::FilePath versions_dir,
    std::string workflow_id,
    std::string timestamp) {
  base::FilePath dir =
      WorkflowVersionsSubdirForOnBackgroundSequence(versions_dir, workflow_id);
  base::FilePath file = dir.AppendASCII(
      base::StrCat({SanitizeForFileName(timestamp), ".json"}));
  std::string contents;
  if (!base::ReadFileToString(file, &contents)) {
    return std::nullopt;
  }
  return contents;
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

net::NetworkTrafficAnnotationTag GetMcpDiscoveryTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("n8n_mcp_workflow_discovery", R"(
      semantics {
        sender: "n8n Process Manager"
        description:
          "Lists the user's local n8n workflows to find which ones expose "
          "an MCP Server Trigger, for the Settings \"n8n\" page and the AI "
          "Assistant chat menu's \"Active MCPs\" list."
        trigger:
          "The user opens the n8n Settings section or the chat menu's "
          "Active MCPs list."
        data: "None sent beyond an API key header; localhost only."
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
          "Only ever talks to a localhost n8n instance the browser itself "
          "started."
      })");
}

// Extracts every activated workflow with an MCP Server Trigger node from
// n8n's `GET /api/v1/workflows` response - which may be either
// `{"data": [...]}` (the documented Public API shape) or a bare array,
// depending on n8n version, so both are accepted defensively. Mirrors
// n8n_mcp_tools.cc's ExtractMcpWorkflows, but also keeps each workflow's
// id (needed to key the enable/disable pref) and its enabled state.
std::vector<N8nProcessManager::McpWorkflowInfo> ExtractMcpWorkflowInfos(
    const base::Value& body,
    const std::string& base_url,
    PrefService* prefs) {
  std::vector<N8nProcessManager::McpWorkflowInfo> result;
  const base::ListValue* list = nullptr;
  if (body.is_dict()) {
    list = body.GetDict().FindList("data");
  } else if (body.is_list()) {
    list = &body.GetList();
  }
  if (!list) {
    return result;
  }
  for (const auto& workflow_value : *list) {
    if (!workflow_value.is_dict()) {
      continue;
    }
    const base::DictValue& workflow = workflow_value.GetDict();
    if (!workflow.FindBool("active").value_or(false)) {
      continue;
    }
    const base::ListValue* nodes = workflow.FindList("nodes");
    if (!nodes) {
      continue;
    }
    for (const auto& node_value : *nodes) {
      if (!node_value.is_dict()) {
        continue;
      }
      const base::DictValue& node = node_value.GetDict();
      const std::string* type = node.FindString("type");
      if (!type || *type != kMcpTriggerNodeType) {
        continue;
      }
      std::string path;
      if (const base::DictValue* params = node.FindDict("parameters")) {
        if (const std::string* path_param = params->FindString("path")) {
          path = *path_param;
        }
      }
      if (path.empty()) {
        if (const std::string* webhook_id = node.FindString("webhookId")) {
          path = *webhook_id;
        }
      }
      if (path.empty()) {
        continue;
      }
      const std::string* id = workflow.FindString("id");
      const std::string* name = workflow.FindString("name");
      N8nProcessManager::McpWorkflowInfo info;
      info.id = id ? *id : "";
      info.name = name ? *name : "(unnamed workflow)";
      info.mcp_url = base::StrCat({base_url, "/mcp/", path});
      info.enabled =
          info.id.empty() ||
          N8nProcessManager::IsMcpWorkflowEnabled(prefs, info.id);
      result.push_back(std::move(info));
      break;  // Only one MCP trigger per workflow is meaningful.
    }
  }
  return result;
}

}  // namespace

N8nProcessManager::McpWorkflowInfo::McpWorkflowInfo() = default;
N8nProcessManager::McpWorkflowInfo::McpWorkflowInfo(McpWorkflowInfo&&) =
    default;
N8nProcessManager::McpWorkflowInfo& N8nProcessManager::McpWorkflowInfo::
operator=(McpWorkflowInfo&&) = default;
N8nProcessManager::McpWorkflowInfo::~McpWorkflowInfo() = default;

// static
void N8nProcessManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kN8nApiKeyPref, std::string());
  registry->RegisterListPref(kN8nDisabledMcpWorkflowsPref);
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
void N8nProcessManager::SetMcpWorkflowEnabled(PrefService* prefs,
                                              const std::string& workflow_id,
                                              bool enabled) {
  if (!prefs || workflow_id.empty()) {
    return;
  }
  ScopedListPrefUpdate update(prefs, kN8nDisabledMcpWorkflowsPref);
  if (enabled) {
    update->EraseValue(base::Value(workflow_id));
  } else if (!IsMcpWorkflowEnabled(prefs, workflow_id)) {
    return;  // Already disabled - avoid a duplicate entry.
  } else {
    update->Append(workflow_id);
  }
}

// static
bool N8nProcessManager::IsMcpWorkflowEnabled(PrefService* prefs,
                                             const std::string& workflow_id) {
  if (!prefs || workflow_id.empty()) {
    return true;
  }
  const base::ListValue& disabled =
      prefs->GetList(kN8nDisabledMcpWorkflowsPref);
  return std::ranges::find(disabled, base::Value(workflow_id)) ==
         disabled.end();
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

// static
base::FilePath N8nProcessManager::GetWorkflowVersionsDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &user_data_dir);
  return user_data_dir.Append(FILE_PATH_LITERAL("BraveN8nWorkflowVersions"));
}

void N8nProcessManager::SaveWorkflowVersionSnapshot(
    const std::string& workflow_id,
    const std::string& workflow_json,
    SaveWorkflowVersionCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&SaveWorkflowVersionSnapshotOnBackgroundSequence,
                     GetWorkflowVersionsDir(), workflow_id, workflow_json),
      std::move(callback));
}

void N8nProcessManager::ListWorkflowVersions(
    const std::string& workflow_id,
    ListWorkflowVersionsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ListWorkflowVersionsOnBackgroundSequence,
                     GetWorkflowVersionsDir(), workflow_id),
      std::move(callback));
}

void N8nProcessManager::ReadWorkflowVersionSnapshot(
    const std::string& workflow_id,
    const std::string& timestamp,
    ReadWorkflowVersionCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ReadWorkflowVersionSnapshotOnBackgroundSequence,
                     GetWorkflowVersionsDir(), workflow_id, timestamp),
      std::move(callback));
}

void N8nProcessManager::ExportLatestBackupToFile(
    const base::FilePath& destination_zip_path,
    ExportBackupCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ExportLatestBackupOnBackgroundSequence, GetDataDir(),
                     GetBackupDir(), destination_zip_path),
      base::BindOnce(
          [](ExportBackupCallback callback,
             std::pair<bool, std::string> result) {
            std::move(callback).Run(result.first, std::move(result.second));
          },
          std::move(callback)));
}

void N8nProcessManager::ImportBackupFromFile(
    const base::FilePath& backup_zip_path,
    ImportBackupCallback callback) {
  if (IsRunning()) {
    std::move(callback).Run(
        false, "Stop n8n before importing a backup (it's currently "
              "running).");
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ImportBackupOnBackgroundSequence, backup_zip_path,
                     GetDataDir()),
      base::BindOnce(
          [](ImportBackupCallback callback,
             std::pair<bool, std::string> result) {
            std::move(callback).Run(result.first, std::move(result.second));
          },
          std::move(callback)));
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
  EnsureSidebarItemRegistered();
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

void N8nProcessManager::EnsureSidebarItemRegistered() {
  Profile* profile = Profile::FromBrowserContext(browser_context_);
  if (!profile) {
    return;
  }
  auto* sidebar_service =
      sidebar::SidebarServiceFactory::GetForProfile(profile);
  if (!sidebar_service) {
    return;
  }
  GURL n8n_url(
      base::StrCat({"http://localhost:", base::NumberToString(kN8nPort)}));
  for (const auto& item : sidebar_service->items()) {
    if (item.url == n8n_url) {
      return;  // Already registered - a web item's URL is its id.
    }
  }
  sidebar_service->AddItem(sidebar::SidebarItem::Create(
      n8n_url, u"n8n", sidebar::SidebarItem::Type::kTypeWeb,
      sidebar::SidebarItem::BuiltInItemType::kNone,
      /*open_in_panel=*/true));
}

void N8nProcessManager::EnsureBackupScheduledTaskRegistered() {
  if (backup_task_registration_attempted_) {
    return;
  }
  backup_task_registration_attempted_ = true;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&RegisterBackupScheduledTaskOnBackgroundSequence,
                     GetDataDir(), GetBackupDir()));
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

  EnsureBackupScheduledTaskRegistered();

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

void N8nProcessManager::ListMcpWorkflows(ListMcpWorkflowsCallback callback) {
  if (!IsReady()) {
    // Deliberately not calling EnsureStarted() here - a Settings page or
    // chat menu listing shouldn't launch n8n as a side effect of being
    // opened. An empty list is the correct answer if n8n isn't running.
    std::move(callback).Run(true, "", {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key = prefs ? GetApiKey(prefs) : "";
  if (api_key.empty()) {
    std::move(callback).Run(false, "No n8n API key stored yet.", {});
    return;
  }
  if (!mcp_discovery_helper_) {
    auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    mcp_discovery_helper_ =
        std::make_unique<api_request_helper::APIRequestHelper>(
            GetMcpDiscoveryTrafficAnnotationTag(),
            std::move(url_loader_factory));
  }
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  mcp_discovery_helper_->Request(
      "GET", GURL(base::StrCat({base_url_, "/api/v1/workflows"})), "", "",
      base::BindOnce(&N8nProcessManager::OnMcpWorkflowsListed,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void N8nProcessManager::OnMcpWorkflowsListed(
    ListMcpWorkflowsCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        false,
        base::StrCat({"n8n returned ",
                      base::NumberToString(result.response_code()),
                      " listing workflows."}),
        {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::move(callback).Run(
      true, "",
      ExtractMcpWorkflowInfos(result.value_body(), base_url_, prefs));
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

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_
#define BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/process/process.h"
#include "base/timer/timer.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Launches and supervises a local n8n instance (a Node.js process, run via
// `npx n8n`) as a child process, and exposes its URL to the side panel and
// AI Chat tools that talk to its REST API. One instance per profile via
// N8nProcessManagerFactory, started lazily on first use (not automatically
// on browser startup) - see brave-n8n-mcp-integration.md.
//
// n8n's own data (its SQLite database of workflows, credentials, etc.) is
// stored via the N8N_USER_FOLDER env var at a fixed path *outside* both the
// browser profile and anything the installer's uninstaller would remove -
// this is what Phase 3's backup/restore will build on, and is set up now so
// nothing needs to move later.
class N8nProcessManager : public KeyedService {
 public:
  using StartedCallback = base::OnceCallback<void(bool success)>;

  // Observes the n8n process's captured console output and running state,
  // for the in-browser terminal view (see the Settings "n8n" page) - n8n is
  // always launched with no native console window (see LaunchProcessAndReport),
  // so this is the only place its stdout/stderr become visible to the user.
  class Observer : public base::CheckedObserver {
   public:
    // `text` is a chunk of newly-captured stdout/stderr, not necessarily
    // line-aligned - the terminal view appends chunks as they arrive.
    virtual void OnN8nOutputAppended(const std::string& text) {}
    virtual void OnN8nRunningStateChanged(bool running) {}
  };

  explicit N8nProcessManager(content::BrowserContext* browser_context);
  ~N8nProcessManager() override;

  N8nProcessManager(const N8nProcessManager&) = delete;
  N8nProcessManager& operator=(const N8nProcessManager&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // n8n requires authentication for both its internal editor API (session
  // cookie, from logging in) and its versioned Public API (an API key) -
  // there's no way around this from outside, it's n8n's own first-run
  // "owner account" setup. The Public API + a stored key is what
  // create_n8n_workflow/run_n8n_workflow use, since faking a browser
  // session isn't reasonable; the user generates the key once via n8n's
  // own UI (Settings > n8n API, after completing owner setup) and gives it
  // to the assistant, which stores it here.
  static void SetApiKey(PrefService* prefs, const std::string& api_key);
  static std::string GetApiKey(PrefService* prefs);

  // One activated workflow found to expose an MCP Server Trigger node,
  // with the URL its MCP endpoint is reachable at and whether the browser
  // currently allows the AI to use it (see SetMcpWorkflowEnabled - this is
  // independent of n8n's own "active" state).
  struct McpWorkflowInfo {
    McpWorkflowInfo();
    McpWorkflowInfo(McpWorkflowInfo&&);
    McpWorkflowInfo& operator=(McpWorkflowInfo&&);
    ~McpWorkflowInfo();

    std::string id;
    std::string name;
    std::string mcp_url;
    bool enabled = true;
  };
  using ListMcpWorkflowsCallback =
      base::OnceCallback<void(bool success,
                              std::string error_message,
                              std::vector<McpWorkflowInfo> workflows)>;

  // Discovers this profile's activated n8n workflows that expose an MCP
  // Server Trigger node, via n8n's own REST API - the same discovery
  // ListN8nMcpToolsTool uses, but exposed here so the Settings "n8n" page
  // and the AI Assistant chat menu's "Active MCPs" list can show the same
  // information without starting n8n themselves (a Settings/chat page
  // shouldn't launch a background process just to render a list - the
  // list is simply empty/unreachable if n8n isn't running).
  void ListMcpWorkflows(ListMcpWorkflowsCallback callback);

  // Browser-side "connect/disconnect" toggle for one workflow, keyed by
  // its n8n workflow id - independent of n8n's own active/inactive state.
  // Turned off, ListN8nMcpToolsTool/CallN8nMcpToolTool won't expose or
  // call that workflow's tools even though n8n itself still has it
  // activated. Any workflow not explicitly disabled defaults to enabled.
  static void SetMcpWorkflowEnabled(PrefService* prefs,
                                    const std::string& workflow_id,
                                    bool enabled);
  static bool IsMcpWorkflowEnabled(PrefService* prefs,
                                   const std::string& workflow_id);

  // Launches n8n if it isn't already running, and reports success once its
  // web server actually answers an HTTP request - not just once the OS
  // process has started, which can be well before `npx` has finished
  // resolving/installing n8n (a cold install) or before n8n's own boot
  // (SQLite migrations, etc.) completes. Polls every kHealthCheckInterval
  // for up to kMaxHealthCheckAttempts before giving up and reporting
  // failure (the process is left running either way - check
  // GetBufferedOutput() or the Settings "n8n" terminal view for why it
  // didn't come up). Concurrent callers while a launch/poll is already in
  // flight are queued, not given a second process. No-ops (reports success
  // immediately) if already confirmed ready - see IsReady().
  void EnsureStarted(StartedCallback callback);

  // True once the OS process object is valid - does NOT mean n8n's web
  // server is actually answering requests yet. See IsReady() for that.
  bool IsRunning() const;

  // True once EnsureStarted's health check has actually gotten an HTTP
  // response from n8n. This is what "Running" should mean to a user -
  // use this, not IsRunning(), for any UI status display.
  bool IsReady() const { return is_ready_; }

  // The base URL of the running instance (e.g. http://localhost:5678),
  // valid only once EnsureStarted has reported success.
  const std::string& base_url() const { return base_url_; }

  // The directory n8n stores its own data in (workflows, credentials,
  // execution history) - fixed, outside the browser profile.
  static base::FilePath GetDataDir();

  // Where daily backups of GetDataDir() are written - also fixed, outside
  // both the browser profile and GetDataDir() itself. Both paths sit under
  // %LOCALAPPDATA% directly (not under the browser's own "User Data"
  // profile tree the installer's uninstaller manages), which is what
  // makes them survive an uninstall/reinstall in the first place - not any
  // special installer hook.
  static base::FilePath GetBackupDir();

  // Zips GetDataDir() into GetBackupDir(), timestamped, and prunes old
  // backups beyond kMaxBackupsToKeep. Runs the zip on a background
  // sequence. Safe to call manually (e.g. a future Settings "back up now"
  // button); also called automatically once a day - see the timer set up
  // in the constructor.
  void PerformBackup();

  // If GetDataDir() is missing/empty and at least one backup exists,
  // restores the most recent one into it. Called once, early in
  // EnsureStarted(), before n8n is actually launched - this is the
  // restore-on-reinstall mechanism: a fresh install has an empty data dir,
  // so if backups already exist at the (uninstall-surviving) backup path,
  // they get restored automatically before n8n ever starts.
  void MaybeRestoreFromBackup(base::OnceClosure done);

  // Registers (or re-registers, idempotently) a Windows Scheduled Task
  // that backs up GetDataDir() once a day even if the browser itself is
  // never open at that time - the daily base::RepeatingTimer above only
  // fires while this process is alive, which doesn't help a user who
  // rarely leaves the browser running. The task shells out to
  // powershell.exe to zip the folder directly - it doesn't invoke
  // brave.exe at all, so it works regardless of whether the browser (or
  // this KeyedService) is running. Best-effort: failures (e.g.
  // schtasks.exe missing, Task Scheduler service disabled by policy) are
  // silently ignored, since the in-browser daily timer remains the
  // primary backup mechanism. Called once per browser session, the first
  // time n8n is actually started.
  //
  // Known limitation: nothing currently removes this task if the browser
  // is uninstalled - doing that reliably needs an installer-level
  // uninstall hook, a separately-scoped change. The task's own backup
  // target (GetDataDir()) is deliberately left alone by the uninstaller
  // already (see GetBackupDir()'s comment), so the task keeps backing up
  // real data even post-uninstall; it just needs manual removal
  // (`schtasks /Delete /TN BraveN8nDailyBackup`) if that's undesired.
  void EnsureBackupScheduledTaskRegistered();

  // Adds a "n8n" entry to the profile's sidebar (name + n8n's own favicon
  // as the icon, once it loads), pointing at n8n's fixed local URL and
  // opening in a side panel - Brave's existing generic web-panel
  // mechanism (kSidebarWebPanel, enabled by default in this fork), not a
  // dedicated Chromium-patched side panel. Idempotent - checks for an
  // existing item with the same URL first, since a URL is a web item's
  // id. Called once from the constructor, so the entry point into n8n is
  // discoverable even before it's ever been started; if n8n isn't
  // running yet when the user opens the panel, it'll just show a
  // connection error until they start it from the "n8n" Settings page or
  // ask the AI Assistant - this doesn't auto-start n8n on its own.
  void EnsureSidebarItemRegistered();

  // Where per-workflow version snapshots are stored (one JSON file per
  // snapshot, filenamed by timestamp) - outside both the browser profile
  // and n8n's own data dir, alongside GetBackupDir(). n8n's own workflow
  // versioning is an enterprise-only feature; this gives simple version
  // history/rollback without it - see UpdateN8nWorkflowTool and
  // RollbackN8nWorkflowTool (browser/ai_chat/tools/n8n_tools.h).
  static base::FilePath GetWorkflowVersionsDir();

  // Snapshots `workflow_json` for `workflow_id`, timestamped. `workflow_id`
  // is sanitized to a safe filename component before touching disk.
  using SaveWorkflowVersionCallback = base::OnceCallback<void(bool success)>;
  void SaveWorkflowVersionSnapshot(const std::string& workflow_id,
                                   const std::string& workflow_json,
                                   SaveWorkflowVersionCallback callback);

  // Every snapshot saved for `workflow_id`, oldest first. The timestamp
  // string doubles as the version's id for ReadWorkflowVersionSnapshot.
  using ListWorkflowVersionsCallback =
      base::OnceCallback<void(std::vector<std::string> timestamps)>;
  void ListWorkflowVersions(const std::string& workflow_id,
                            ListWorkflowVersionsCallback callback);

  // Reads back one snapshot's JSON, or nullopt if it doesn't exist.
  using ReadWorkflowVersionCallback =
      base::OnceCallback<void(std::optional<std::string> workflow_json)>;
  void ReadWorkflowVersionSnapshot(const std::string& workflow_id,
                                   const std::string& timestamp,
                                   ReadWorkflowVersionCallback callback);

  // "Multi-machine sync" without any dedicated sync backend: this browser
  // has no infrastructure for syncing arbitrary files between machines
  // (Brave Sync only syncs specific typed data it already knows about),
  // so the practical mechanism is exporting a backup zip to a folder the
  // user already syncs some other way (a cloud-synced folder, a USB
  // drive), then importing it on the other machine. GetBackupDir() itself
  // can also just be pointed at directly by the user's own sync client -
  // these two methods are for the common case of wanting one explicit
  // file to move around instead.
  //
  // Copies the most recent backup under GetBackupDir() to
  // `destination_zip_path` (running a fresh PerformBackup() first if none
  // exists yet).
  using ExportBackupCallback =
      base::OnceCallback<void(bool success, std::string message)>;
  void ExportLatestBackupToFile(const base::FilePath& destination_zip_path,
                                ExportBackupCallback callback);

  // Restores GetDataDir() from an arbitrary backup zip file - not
  // necessarily one this machine created, e.g. one imported from another
  // machine via ExportLatestBackupToFile there. Refuses while n8n is
  // running (IsRunning()) - the data dir must be quiescent to safely
  // overwrite.
  using ImportBackupCallback =
      base::OnceCallback<void(bool success, std::string message)>;
  void ImportBackupFromFile(const base::FilePath& backup_zip_path,
                            ImportBackupCallback callback);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Everything captured from n8n's stdout/stderr so far this run (capped -
  // see kMaxOutputBufferBytes), for a newly-opened terminal view to
  // backfill before it starts receiving live OnN8nOutputAppended() calls.
  const std::string& GetBufferedOutput() const { return output_buffer_; }

  // KeyedService:
  void Shutdown() override;

 private:
  void OnRestoreCheckComplete(base::OnceClosure done,
                              std::optional<base::FilePath> backup_to_restore);
  void OnRestoreComplete(base::OnceClosure done, bool success);
  void LaunchProcessAndReport();
  void AppendOutput(std::string chunk);
  void PollForReady(int attempts_remaining);
  void OnHealthCheckResponse(int attempts_remaining,
                             api_request_helper::APIRequestResult result);
  void ResolvePendingStartedCallbacks(bool success);
  void OnMcpWorkflowsListed(ListMcpWorkflowsCallback callback,
                            api_request_helper::APIRequestResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::Process process_;
  std::string base_url_;
  int port_ = 0;
  bool restore_checked_ = false;
  bool is_ready_ = false;
  bool backup_task_registration_attempted_ = false;
  std::vector<StartedCallback> pending_started_callbacks_;
  std::unique_ptr<api_request_helper::APIRequestHelper> health_check_helper_;
  std::unique_ptr<api_request_helper::APIRequestHelper> mcp_discovery_helper_;
  base::RepeatingTimer backup_timer_;

  // Captured n8n console output, and the observers watching it live (the
  // Settings "n8n" page's terminal view). n8n never gets a native console
  // window of its own - see LaunchProcessAndReport - so this buffer is the
  // only way its output is ever visible.
  std::string output_buffer_;
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<N8nProcessManager> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_

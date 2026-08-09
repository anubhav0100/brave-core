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

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::Process process_;
  std::string base_url_;
  int port_ = 0;
  bool restore_checked_ = false;
  bool is_ready_ = false;
  std::vector<StartedCallback> pending_started_callbacks_;
  std::unique_ptr<api_request_helper::APIRequestHelper> health_check_helper_;
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

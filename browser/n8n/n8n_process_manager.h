// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_
#define BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

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

  N8nProcessManager();
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

  // Launches n8n if it isn't already running, and reports success once the
  // OS process itself started (a short fixed delay is used to give n8n's
  // web server time to come up before the callback fires - `npx n8n` takes
  // a few seconds to boot even after the process starts; this is a
  // simplification over a real HTTP health-check poll, which would need
  // network-service plumbing this doesn't otherwise require. The side
  // panel's own WebView load will show a real error if n8n genuinely isn't
  // ready yet). No-ops (reports success immediately) if already running.
  void EnsureStarted(StartedCallback callback);

  bool IsRunning() const;

  // The base URL of the running instance (e.g. http://localhost:5678),
  // valid only once EnsureStarted has reported success.
  const std::string& base_url() const { return base_url_; }

  // The directory n8n stores its own data in (workflows, credentials,
  // execution history) - fixed, outside the browser profile. Exposed so
  // Phase 3's backup task can find it without duplicating this path logic.
  static base::FilePath GetDataDir();

  // KeyedService:
  void Shutdown() override;

 private:
  base::Process process_;
  std::string base_url_;
  int port_ = 0;

  base::WeakPtrFactory<N8nProcessManager> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_N8N_N8N_PROCESS_MANAGER_H_

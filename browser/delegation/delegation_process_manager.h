// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_H_
#define BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_H_

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
#include "base/values.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "components/keyed_service/core/keyed_service.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Launches and supervises a local instance of "The Delegation"
// (github.com/anubhav0100/the-delegation, a forked copy with a monitoring
// bridge added - see brave-the-delegation-integration.md), a client-side
// multi-agent 3D simulation app, as a child process. Mirrors
// N8nProcessManager's shape (one instance per profile, started lazily,
// health-check polling, sidebar item, hidden-console output capture) with
// one addition: unlike n8n (fetched instantly via `npx`), first use here
// needs a one-time `git clone` + `npm install` + `npm run build` pipeline
// before the app can be served at all - see EnsureStarted().
class DelegationProcessManager : public KeyedService {
 public:
  using StartedCallback = base::OnceCallback<void(bool success)>;

  // Observes captured setup/server console output and running state, for
  // the in-browser terminal view (Settings "Delegation" page) - same
  // pattern as N8nProcessManager::Observer.
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnDelegationOutputAppended(const std::string& text) {}
    virtual void OnDelegationRunningStateChanged(bool running) {}
  };

  explicit DelegationProcessManager(content::BrowserContext* browser_context);
  ~DelegationProcessManager() override;

  DelegationProcessManager(const DelegationProcessManager&) = delete;
  DelegationProcessManager& operator=(const DelegationProcessManager&) =
      delete;

  // Runs the one-time setup pipeline if needed (clone/install/build,
  // skipping any step whose output already exists on disk), then launches
  // the server and polls until it actually answers an HTTP request - not
  // just once the OS process exists. Concurrent callers while a
  // setup/launch/poll is already in flight are queued. No-ops (reports
  // success immediately) if already confirmed ready - see IsReady().
  void EnsureStarted(StartedCallback callback);

  // True once the OS process object for the *server* (not a setup step) is
  // valid - does NOT mean it's actually answering requests yet.
  bool IsRunning() const;

  // True once EnsureStarted's health check has gotten an HTTP response
  // from the server. This is what "Running" should mean to a user.
  bool IsReady() const { return is_ready_; }

  // The base URL of the running instance (e.g. http://localhost:3210),
  // valid only once EnsureStarted has reported success.
  const std::string& base_url() const { return base_url_; }

  // GET /api/state on the running server, parsed into a JSON value - used
  // by GetDelegationStatusTool. Returns nullopt (via the bool) if not
  // ready or the request failed; does not call EnsureStarted() as a side
  // effect, matching N8nProcessManager::ListMcpWorkflows's rule that a
  // status check shouldn't silently launch a background process.
  using GetStateCallback =
      base::OnceCallback<void(bool success, base::DictValue state)>;
  void GetState(GetStateCallback callback);

  // POST /api/control with {action, payload} - used by the
  // approve/reject/inject AI Chat tools.
  using SendControlCallback =
      base::OnceCallback<void(bool success, std::string error_message)>;
  void SendControlAction(const std::string& action,
                         base::DictValue payload,
                         SendControlCallback callback);

  // Adds a "Delegation" entry to the profile's sidebar, same mechanism as
  // N8nProcessManager::EnsureSidebarItemRegistered - Brave's existing
  // generic web-panel mechanism, idempotent via URL-as-id. Called once
  // from the constructor.
  void EnsureSidebarItemRegistered();

  // Everything captured from the setup pipeline and server's stdout/stderr
  // so far this run, for a newly-opened terminal view to backfill.
  const std::string& GetBufferedOutput() const { return output_buffer_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // KeyedService:
  void Shutdown() override;

 private:
  void OnSetupPipelineComplete(bool success, std::string combined_output);
  void LaunchServerAndReport();
  void AppendOutput(std::string chunk);
  void PollForReady(int attempts_remaining);
  void OnHealthCheckResponse(int attempts_remaining,
                             api_request_helper::APIRequestResult result);
  void ResolvePendingStartedCallbacks(bool success);
  void OnStateResponse(GetStateCallback callback,
                       api_request_helper::APIRequestResult result);
  void OnControlResponse(SendControlCallback callback,
                         api_request_helper::APIRequestResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::Process process_;
  std::string base_url_;
  int port_ = 0;
  bool setup_started_ = false;
  bool is_ready_ = false;
  std::vector<StartedCallback> pending_started_callbacks_;
  std::unique_ptr<api_request_helper::APIRequestHelper> health_check_helper_;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_helper_;

  std::string output_buffer_;
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<DelegationProcessManager> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_H_

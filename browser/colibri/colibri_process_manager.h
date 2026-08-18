// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_H_
#define BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_H_

#include <memory>
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

// Launches and supervises a local instance of Colibri
// (github.com/anubhav0100/colibri, a C inference engine that exposes an
// OpenAI-compatible HTTP API via its `coli serve` launcher) as a child
// process, given a folder the user already unpacked a prebuilt Colibri
// release into and a folder containing a model they already downloaded -
// see docs/api.md and the README's "Get colibri" section in that repo.
//
// Mirrors DelegationProcessManager's shape (one instance per profile,
// started lazily, health-check polling, hidden-console output capture),
// minus the one-time clone/install/build pipeline: unlike The Delegation,
// Colibri's engine and models are both too large/hardware-specific for the
// browser to fetch on the user's behalf, so EnsureStarted() takes the two
// paths directly instead of assuming anything is already on disk at a
// fixed location.
class ColibriProcessManager : public KeyedService {
 public:
  using StartedCallback = base::OnceCallback<void(bool success)>;

  // Observes the launched Colibri process's captured console output and
  // running state, for the in-browser terminal view (Settings "Your
  // models" page) - same pattern as N8nProcessManager::Observer /
  // DelegationProcessManager::Observer.
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnColibriOutputAppended(const std::string& text) {}
    virtual void OnColibriRunningStateChanged(bool running) {}
  };

  explicit ColibriProcessManager(content::BrowserContext* browser_context);
  ~ColibriProcessManager() override;

  ColibriProcessManager(const ColibriProcessManager&) = delete;
  ColibriProcessManager& operator=(const ColibriProcessManager&) = delete;

  // Launches `coli serve` out of `colibri_dir` (the folder containing the
  // `coli` launcher and `colibri.exe`, from a prebuilt release) against
  // `model_dir` (the folder containing the user's downloaded model), and
  // reports success once the server actually answers an HTTP request - not
  // just once the OS process exists, which can be well before a 300GB+
  // model has finished loading into memory. Polls every
  // kHealthCheckInterval for up to kMaxHealthCheckAttempts. Concurrent
  // callers while a launch/poll is already in flight are queued. No-ops
  // (reports success immediately) if already confirmed ready - see
  // IsReady().
  //
  // `model_dir` may be empty - newer Colibri releases accept `coli serve`
  // with no `--model` at all and come up with no engine loaded, which is
  // what lets DownloadModel() below run against a freshly-started instance
  // that doesn't have a model on disk yet.
  void EnsureStarted(const base::FilePath& colibri_dir,
                     const base::FilePath& model_dir,
                     StartedCallback callback);

  // Stops the current process (if any) and relaunches `coli serve` out of
  // the same `colibri_dir` EnsureStarted was last called with, this time
  // pointed at `model_dir` - for loading a model that DownloadModel() just
  // finished fetching, since the running engine is fixed at process
  // startup and won't pick up a newly-downloaded model on its own.
  void RestartWithModel(const base::FilePath& model_dir,
                        StartedCallback callback);

  // True once the OS process object is valid - does NOT mean Colibri's web
  // server is actually answering requests yet (a large model can take a
  // long time to load). See IsReady() for that.
  bool IsRunning() const;

  // True once EnsureStarted's health check has actually gotten an HTTP
  // response from Colibri. This is what "Running" should mean to a user.
  bool IsReady() const { return is_ready_; }

  // The base URL of the running instance (http://127.0.0.1:8000), valid
  // only once EnsureStarted has reported success.
  const std::string& base_url() const { return base_url_; }

  // Everything captured from Colibri's stdout/stderr so far this run
  // (capped - see kMaxOutputBufferBytes), for a newly-opened terminal view
  // to backfill before it starts receiving live
  // OnColibriOutputAppended() calls.
  const std::string& GetBufferedOutput() const { return output_buffer_; }

  // Newer Colibri releases expose a `/v1/setup/pipeline` endpoint on the
  // running instance that downloads+converts a HuggingFace model in a
  // background subprocess of its own, so the browser doesn't need any
  // download logic of its own - only to drive this API. Requires Colibri
  // to already be running (IsReady()); these are calls against the live
  // server, not something EnsureStarted needs to know about.
  //
  // `state` mirrors the server's own shape:
  // {status: "idle"|"running"|"success"|"error", step, progress (0-100),
  //  repo, outdir, logs: [string]}.
  using DownloadModelCallback =
      base::OnceCallback<void(bool success,
                              std::string message,
                              base::DictValue state)>;

  // Starts (or, if one's already running and `force` is false, just
  // attaches to) a download of `repo` (a HuggingFace repo id) into
  // `outdir` (relative to the running Colibri instance's own working
  // directory - i.e. `colibri_dir`).
  void DownloadModel(const std::string& repo,
                     const std::string& outdir,
                     bool force,
                     DownloadModelCallback callback);

  // Terminates an in-progress download.
  void StopDownload(DownloadModelCallback callback);

  using GetDownloadStateCallback =
      base::OnceCallback<void(bool success, base::DictValue state)>;

  // Polls the current download state - safe to call whether or not a
  // download is in progress (status is "idle" if nothing's running).
  void GetDownloadState(GetDownloadStateCallback callback);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // KeyedService:
  void Shutdown() override;

 private:
  void LaunchProcessAndReport(const base::FilePath& colibri_dir,
                              const base::FilePath& model_dir);
  void AppendOutput(std::string chunk);
  void PollForReady(int attempts_remaining);
  void OnHealthCheckResponse(int attempts_remaining,
                             api_request_helper::APIRequestResult result);
  void ResolvePendingStartedCallbacks(bool success);
  void EnsureApiHelper();
  void OnDownloadPipelineResponse(DownloadModelCallback callback,
                                  api_request_helper::APIRequestResult result);
  void OnDownloadStateResponse(GetDownloadStateCallback callback,
                               api_request_helper::APIRequestResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::Process process_;
  base::FilePath colibri_dir_;
  std::string base_url_;
  bool is_ready_ = false;
  std::vector<StartedCallback> pending_started_callbacks_;
  std::unique_ptr<api_request_helper::APIRequestHelper> health_check_helper_;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_helper_;

  std::string output_buffer_;
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<ColibriProcessManager> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_H_

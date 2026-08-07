// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_SIDE_PANEL_COORDINATOR_H_
#define BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_SIDE_PANEL_COORDINATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "brave/browser/ai_chat/full_page_source_fetcher.h"
#include "brave/components/ai_chat/core/browser/engine/engine_consumer.h"

class BrowserWindowInterface;
class Profile;
class SidePanelRegistry;
class SidePanelEntryScope;

namespace views {
class View;
}  // namespace views

namespace ai_chat {
class ModelService;
}  // namespace ai_chat

namespace page_capture {

// One page the user has captured this session: `heading` is the page's
// title/URL, `original_content` is the raw text read from the page,
// `refined_content` is the LLM's refined/summarized version of it.
struct CapturedEntry {
  std::string heading;
  std::string original_content;
  std::string refined_content;
};

// One of the user's own configured (Bring-Your-Own-Model) models, as offered
// in the panel's model picker.
struct AvailableModel {
  std::string key;
  std::string display_name;
};

// One entry in the coordinator's activity log - a timestamped record of
// what this feature did, surfaced in Settings for diagnosing failures
// (timeouts, API errors, etc.) without needing to reproduce them live.
struct LogEntry {
  base::Time timestamp;
  std::string message;
};

// Owns the "Page Capture" side panel's session state for the lifetime of a
// browser window - the accumulated list of captured+refined pages survives
// the panel being closed and reopened, only cleared explicitly by the user
// (ClearSession()) or when the window closes. Mirrors the shape of
// PlaylistSidePanelCoordinator, but produces a plain native views::View
// (PageCaptureView) instead of a WebUI-backed one, since this feature has no
// need for HTML/CSS/TypeScript - see the implementation plan for why a
// native Views panel was chosen over a new WebUI subsystem.
class PageCaptureSidePanelCoordinator {
 public:
  // `error_message` is only set when `success` is false.
  using CaptureCallback =
      base::OnceCallback<void(bool success, std::string error_message)>;

  PageCaptureSidePanelCoordinator(BrowserWindowInterface* browser,
                                  Profile* profile);
  PageCaptureSidePanelCoordinator(const PageCaptureSidePanelCoordinator&) =
      delete;
  PageCaptureSidePanelCoordinator& operator=(
      const PageCaptureSidePanelCoordinator&) = delete;
  ~PageCaptureSidePanelCoordinator();

  void CreateAndRegisterEntry(SidePanelRegistry* global_registry);

  const std::vector<CapturedEntry>& session_entries() const {
    return session_entries_;
  }

  // Timestamped record of this coordinator's activity (captures, saves,
  // errors), newest last, capped to a bounded size. Read by the Settings
  // page's "Page Capture" section for diagnostics.
  const std::vector<LogEntry>& activity_log() const { return activity_log_; }

  // The user's own configured custom (Bring-Your-Own-Model) models - the
  // only ones this feature ever uses. Never includes Leo-hosted models.
  std::vector<AvailableModel> GetAvailableModels() const;

  // For PageCaptureView to observe model-list changes (add/remove/edit a
  // custom model in Settings) so its model picker can refresh live.
  ai_chat::ModelService* GetModelService() const;

  // Which of GetAvailableModels() to use for the next CaptureCurrentTab()
  // call. Empty means "no explicit choice yet" - CaptureCurrentTab() then
  // falls back to the user's default model (if it's a custom one) or the
  // first configured custom model.
  const std::string& selected_model_key() const { return selected_model_key_; }
  void SetSelectedModel(std::string model_key);

  // Fetches the active tab's page content, sends it to the user's default
  // custom (Bring-Your-Own-Model) model for summarization/refinement, and
  // appends the result to `session_entries_` on success. Only one capture
  // may be in flight at a time; `callback` fires once it's done (either way).
  void CaptureCurrentTab(CaptureCallback callback);

  // Builds a single Word document from all of `session_entries_` (one
  // heading + body per captured page, in capture order) and triggers a
  // native download of it. No-op (silently) if there's nothing captured yet.
  // `callback` reports whether the download was actually started - not
  // whether it fully finished, since the underlying DownloadManager call is
  // itself fire-and-forget.
  void SaveAsWordDocument(CaptureCallback callback);

  // Empties `session_entries_`, starting a fresh session.
  void ClearSession();

 private:
  std::unique_ptr<views::View> CreateView(SidePanelEntryScope& scope);

  void Log(std::string message);

  void OnFullPageSourceFetched(CaptureCallback callback,
                               std::string title,
                               FullPageSource source);
  void OnRefinementComplete(CaptureCallback callback,
                            std::string title,
                            std::string original_content,
                            ai_chat::EngineConsumer::GenerationResult result);
  // This model streams its answer in chunks (SupportsDeltaTextResponses()),
  // in which case the "completed" event OnRefinementComplete() gets carries
  // no text at all by design - the caller is expected to have collected it
  // from these chunks as they arrived. Appends each chunk's delta text.
  void OnRefinementDataReceived(
      ai_chat::EngineConsumer::GenerationResultData result_data);

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<Profile> profile_;

  std::vector<CapturedEntry> session_entries_;
  std::vector<LogEntry> activity_log_;
  std::string selected_model_key_;
  std::string pending_completion_text_;

  // True for the duration of a single in-flight CaptureCurrentTab() call.
  bool capture_in_progress_ = false;
  std::unique_ptr<ai_chat::EngineConsumer> pending_engine_;

  base::WeakPtrFactory<PageCaptureSidePanelCoordinator> weak_ptr_factory_{
      this};
};

}  // namespace page_capture

#endif  // BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_SIDE_PANEL_COORDINATOR_H_

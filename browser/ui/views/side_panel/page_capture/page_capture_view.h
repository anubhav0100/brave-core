// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_VIEW_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/model_service.h"
#include "ui/views/view.h"

namespace ui {
class SimpleComboboxModel;
}  // namespace ui

namespace views {
class Combobox;
class Label;
class MdTextButton;
class View;
}  // namespace views

namespace page_capture {

class PageCaptureSidePanelCoordinator;
struct AvailableModel;

// The native (non-WebUI) contents of the "Page Capture" side panel: a
// capture button, a scrollable list of what's been captured this session,
// and Save/Clear buttons. A new instance is created each time the panel is
// shown (matching the SidePanelEntry::CreateContentCallback contract); the
// actual session data lives on the longer-lived
// `PageCaptureSidePanelCoordinator` this view reads from and calls back
// into, so reopening the panel restores the same session.
class PageCaptureView : public views::View,
                        public ai_chat::ModelService::Observer {
 public:
  explicit PageCaptureView(PageCaptureSidePanelCoordinator* coordinator);
  PageCaptureView(const PageCaptureView&) = delete;
  PageCaptureView& operator=(const PageCaptureView&) = delete;
  ~PageCaptureView() override;

  // ai_chat::ModelService::Observer:
  void OnModelListUpdated() override;

 private:
  void OnCaptureClicked();
  void OnCaptureComplete(bool success, std::string error_message);
  void OnSaveClicked();
  void OnSaveComplete(bool success, std::string error_message);
  void OnClearClicked();
  void OnModelSelectionChanged();

  // Re-reads the coordinator's available custom models and refreshes the
  // combobox's contents/selection - called at construction and again
  // whenever the user adds/removes/edits a custom model in Settings while
  // this panel is open.
  void PopulateModelCombobox();

  // Rebuilds the scrollable list's contents from the coordinator's current
  // session_entries().
  void RefreshList();

  raw_ptr<PageCaptureSidePanelCoordinator> coordinator_;
  raw_ptr<ai_chat::ModelService> model_service_ = nullptr;

  // The models backing model_combobox_, in display order - kept so a
  // selection-changed event can map the chosen index back to a model key.
  std::vector<AvailableModel> models_;
  std::unique_ptr<ui::SimpleComboboxModel> model_combobox_model_;

  raw_ptr<views::Combobox> model_combobox_ = nullptr;
  raw_ptr<views::MdTextButton> capture_button_ = nullptr;
  raw_ptr<views::MdTextButton> save_button_ = nullptr;
  raw_ptr<views::MdTextButton> clear_button_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::View> list_contents_ = nullptr;

  base::WeakPtrFactory<PageCaptureView> weak_ptr_factory_{this};
};

}  // namespace page_capture

#endif  // BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_PAGE_CAPTURE_PAGE_CAPTURE_VIEW_H_

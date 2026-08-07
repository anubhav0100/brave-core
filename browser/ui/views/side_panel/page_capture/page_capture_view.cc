// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/side_panel/page_capture/page_capture_view.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/ui/views/side_panel/page_capture/page_capture_side_panel_coordinator.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace page_capture {

namespace {

constexpr int kPanelMargin = 12;
constexpr int kRowSpacing = 8;
constexpr size_t kBodyPreviewChars = 400;

std::string Truncated(const std::string& text) {
  return text.size() > kBodyPreviewChars
             ? text.substr(0, kBodyPreviewChars) + "..."
             : text;
}

std::u16string EntryPreviewText(const CapturedEntry& entry) {
  return base::UTF8ToUTF16(base::StrCat(
      {entry.heading, "\n\nOriginal:\n",
       Truncated(entry.original_content), "\n\nRefined:\n",
       Truncated(entry.refined_content)}));
}

}  // namespace

PageCaptureView::PageCaptureView(PageCaptureSidePanelCoordinator* coordinator)
    : coordinator_(coordinator) {
  model_service_ = coordinator_->GetModelService();

  views::BoxLayout* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPanelMargin), kRowSpacing));

  auto* model_label = AddChildView(
      std::make_unique<views::Label>(u"Model for this session"));
  model_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  model_combobox_model_ =
      std::make_unique<ui::SimpleComboboxModel>(
          std::vector<ui::SimpleComboboxModel::Item>{});
  model_combobox_ = AddChildView(
      std::make_unique<views::Combobox>(model_combobox_model_.get()));
  model_combobox_->SetCallback(base::BindRepeating(
      &PageCaptureView::OnModelSelectionChanged,
      weak_ptr_factory_.GetWeakPtr()));
  PopulateModelCombobox();

  if (model_service_) {
    model_service_->AddObserver(this);
  }

  // Save/New Session stay fixed near the top, above the growing session
  // list, so they're always reachable no matter how many pages get
  // captured - they used to sit below the list and get pushed out of view.
  auto* button_row = AddChildView(std::make_unique<views::View>());
  button_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kRowSpacing));

  save_button_ = button_row->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&PageCaptureView::OnSaveClicked,
                          weak_ptr_factory_.GetWeakPtr()),
      u"Save as Word Document"));

  clear_button_ =
      button_row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&PageCaptureView::OnClearClicked,
                              weak_ptr_factory_.GetWeakPtr()),
          u"New Session"));

  capture_button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&PageCaptureView::OnCaptureClicked,
                          weak_ptr_factory_.GetWeakPtr()),
      u"Capture & Refine This Tab"));
  capture_button_->SetStyle(ui::ButtonStyle::kProminent);

  status_label_ = AddChildView(std::make_unique<views::Label>(u""));
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetMultiLine(true);

  AddChildView(std::make_unique<views::Separator>());

  // Flexed so the session list scrolls within whatever space remains below
  // the fixed controls above, rather than growing unbounded and pushing
  // everything else out of the visible panel.
  auto* scroll_view = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view->ClipHeightTo(0, 10000);
  layout->SetFlexForView(scroll_view, 1);
  list_contents_ =
      scroll_view->SetContents(std::make_unique<views::View>());
  list_contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacing));

  RefreshList();
}

PageCaptureView::~PageCaptureView() {
  if (model_service_) {
    model_service_->RemoveObserver(this);
  }
}

void PageCaptureView::OnModelListUpdated() {
  PopulateModelCombobox();
}

void PageCaptureView::PopulateModelCombobox() {
  models_ = coordinator_->GetAvailableModels();

  std::vector<ui::SimpleComboboxModel::Item> items;
  if (models_.empty()) {
    items.emplace_back(u"No custom models - add one in Settings");
  } else {
    for (const auto& model : models_) {
      items.emplace_back(base::UTF8ToUTF16(model.display_name));
    }
  }
  model_combobox_model_->UpdateItemList(std::move(items));

  if (models_.empty()) {
    model_combobox_->SetEnabled(false);
    return;
  }

  model_combobox_->SetEnabled(true);
  size_t initial_index = 0;
  for (size_t i = 0; i < models_.size(); ++i) {
    if (models_[i].key == coordinator_->selected_model_key()) {
      initial_index = i;
      break;
    }
  }
  model_combobox_->SetSelectedIndex(initial_index);
  coordinator_->SetSelectedModel(models_[initial_index].key);
}

void PageCaptureView::OnCaptureClicked() {
  capture_button_->SetEnabled(false);
  status_label_->SetText(u"Capturing and refining this page...");
  coordinator_->CaptureCurrentTab(
      base::BindOnce(&PageCaptureView::OnCaptureComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void PageCaptureView::OnCaptureComplete(bool success,
                                        std::string error_message) {
  capture_button_->SetEnabled(true);
  status_label_->SetText(success
                             ? u"Captured. Added to this session's document."
                             : base::UTF8ToUTF16(
                                   base::StrCat({"Error: ", error_message})));
  if (success) {
    RefreshList();
  }
}

void PageCaptureView::OnModelSelectionChanged() {
  std::optional<size_t> index = model_combobox_->GetSelectedIndex();
  if (!index || *index >= models_.size()) {
    return;
  }
  coordinator_->SetSelectedModel(models_[*index].key);
}

void PageCaptureView::OnSaveClicked() {
  save_button_->SetEnabled(false);
  status_label_->SetText(u"Building the Word document...");
  coordinator_->SaveAsWordDocument(base::BindOnce(
      &PageCaptureView::OnSaveComplete, weak_ptr_factory_.GetWeakPtr()));
}

void PageCaptureView::OnSaveComplete(bool success,
                                     std::string error_message) {
  save_button_->SetEnabled(true);
  if (success) {
    status_label_->SetText(u"Saved.");
  } else if (error_message == "Save canceled.") {
    status_label_->SetText(u"Save canceled.");
  } else {
    status_label_->SetText(
        base::UTF8ToUTF16(base::StrCat({"Error: ", error_message})));
  }
}

void PageCaptureView::OnClearClicked() {
  coordinator_->ClearSession();
  status_label_->SetText(u"Started a new session.");
  RefreshList();
}

void PageCaptureView::RefreshList() {
  list_contents_->RemoveAllChildViews();
  for (const auto& entry : coordinator_->session_entries()) {
    auto* label = list_contents_->AddChildView(
        std::make_unique<views::Label>(EntryPreviewText(entry)));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetSelectable(true);
  }
}

}  // namespace page_capture

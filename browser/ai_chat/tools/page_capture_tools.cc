// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/page_capture_tools.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/ai_chat/page_capture_session.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyNameFilename[] = "filename";
}  // namespace

// CapturePageToSessionTool ---------------------------------------------

CapturePageToSessionTool::CapturePageToSessionTool(PageCaptureSession* session)
    : session_(session) {}

CapturePageToSessionTool::~CapturePageToSessionTool() = default;

std::string_view CapturePageToSessionTool::Name() const {
  return mojom::kCapturePageToSessionToolName;
}

std::string_view CapturePageToSessionTool::Description() const {
  return "Captures the ACTUAL source of the current active tab, across all "
         "its frames - split into sections by heading, plus its links and "
         "images - into this conversation's page-capture session. Does not "
         "save anything to disk yet and shows no dialog. You do not write "
         "or retype any of the page's content yourself. Call this once per "
         "page you want to include, then call "
         "save_captured_session_as_word_document to write everything "
         "captured so far to a single Word document (with real embedded "
         "pictures, not just links). Use this instead of "
         "create_word_document whenever the user wants you to save/capture "
         "what's really on a page, since reproducing a page's content "
         "yourself in that tool's arguments is unreliable for anything "
         "longer than a short page.";
}

void CapturePageToSessionTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no browser window available to capture from."),
        {});
    return;
  }
  session_->CaptureActiveTab(base::BindOnce(
      &CapturePageToSessionTool::OnCaptureComplete,
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CapturePageToSessionTool::OnCaptureComplete(UseToolCallback callback,
                                                  bool success,
                                                  std::string message) {
  std::move(callback).Run(CreateContentBlocksForText(message), {});
}

// SaveCapturedSessionAsWordDocumentTool ----------------------------------

SaveCapturedSessionAsWordDocumentTool::SaveCapturedSessionAsWordDocumentTool(
    PageCaptureSession* session)
    : session_(session) {}

SaveCapturedSessionAsWordDocumentTool::
    ~SaveCapturedSessionAsWordDocumentTool() = default;

std::string_view SaveCapturedSessionAsWordDocumentTool::Name() const {
  return mojom::kSaveCapturedSessionAsWordDocumentToolName;
}

std::string_view SaveCapturedSessionAsWordDocumentTool::Description() const {
  return "Builds one Word (.docx) document from every page captured with "
         "capture_page_to_session so far this conversation - each page as "
         "its own titled section, with any images it had embedded as real "
         "pictures - and saves it via a Save As dialog. Picking the same "
         "filename as an existing document lets the user overwrite/update "
         "it. Does not clear the session, so more pages can be captured and "
         "this called again. Fails with an error if nothing has been "
         "captured yet.";
}

std::optional<base::DictValue>
SaveCapturedSessionAsWordDocumentTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".docx extension is added automatically).")}});
}

std::optional<std::vector<std::string>>
SaveCapturedSessionAsWordDocumentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename};
}

void SaveCapturedSessionAsWordDocumentTool::UseTool(
    const std::string& input_json,
    UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no page capture session available."),
        {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const std::string* filename = input->FindString(kPropertyNameFilename);
  if (!filename || filename->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'filename'"), {});
    return;
  }
  session_->SaveAsWordDocument(
      *filename,
      base::BindOnce(&SaveCapturedSessionAsWordDocumentTool::OnSaveComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void SaveCapturedSessionAsWordDocumentTool::OnSaveComplete(
    UseToolCallback callback,
    bool success,
    std::string message) {
  std::move(callback).Run(CreateContentBlocksForText(message), {});
}

// ClearCapturedSessionTool ------------------------------------------------

ClearCapturedSessionTool::ClearCapturedSessionTool(PageCaptureSession* session)
    : session_(session) {}

ClearCapturedSessionTool::~ClearCapturedSessionTool() = default;

std::string_view ClearCapturedSessionTool::Name() const {
  return mojom::kClearCapturedSessionToolName;
}

std::string_view ClearCapturedSessionTool::Description() const {
  return "Clears this conversation's page-capture session, discarding every "
         "page captured with capture_page_to_session so far, so the next "
         "capture starts a fresh document.";
}

void ClearCapturedSessionTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  if (session_) {
    session_->Clear();
  }
  std::move(callback).Run(
      CreateContentBlocksForText("Cleared the page capture session."), {});
}

}  // namespace ai_chat

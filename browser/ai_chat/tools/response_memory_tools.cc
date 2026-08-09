// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/response_memory_tools.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/ai_chat/response_memory_session.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyNameLabel[] = "label";
constexpr char kPropertyNameText[] = "text";
constexpr char kPropertyNameFilename[] = "filename";
}  // namespace

// SaveResponseToMemoryTool -----------------------------------------------

SaveResponseToMemoryTool::SaveResponseToMemoryTool(
    ResponseMemorySession* session)
    : session_(session) {}

SaveResponseToMemoryTool::~SaveResponseToMemoryTool() = default;

std::string_view SaveResponseToMemoryTool::Name() const {
  return mojom::kSaveResponseToMemoryToolName;
}

std::string_view SaveResponseToMemoryTool::Description() const {
  return "Saves a piece of text YOU generated (an answer, a draft, a "
         "summary you just wrote) into this conversation's response-memory "
         "session, under a short label. Does not save anything to disk "
         "yet. Call this once per response you want to keep, then call "
         "save_response_memory_as_word_document to merge everything saved "
         "so far into a single Word document. Use this - not "
         "capture_page_to_session, which is for a page's real fetched "
         "content - when the user wants to keep your own generated text.";
}

std::optional<base::DictValue> SaveResponseToMemoryTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameLabel,
        StringProperty("A short heading for this entry, e.g. what it's an "
                       "answer to or a summary of.")},
       {kPropertyNameText, StringProperty("The text to save.")}});
}

std::optional<std::vector<std::string>>
SaveResponseToMemoryTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameLabel, kPropertyNameText};
}

void SaveResponseToMemoryTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no response memory session available."),
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
  const std::string* label = input->FindString(kPropertyNameLabel);
  const std::string* text = input->FindString(kPropertyNameText);
  if (!label || !text || text->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing or empty 'label' or 'text'"),
        {});
    return;
  }
  session_->AddResponse(*label, *text);
  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Saved '", *label, "' to the response memory session."})),
      {});
}

// SaveResponseMemoryAsWordDocumentTool ------------------------------------

SaveResponseMemoryAsWordDocumentTool::SaveResponseMemoryAsWordDocumentTool(
    ResponseMemorySession* session)
    : session_(session) {}

SaveResponseMemoryAsWordDocumentTool::
    ~SaveResponseMemoryAsWordDocumentTool() = default;

std::string_view SaveResponseMemoryAsWordDocumentTool::Name() const {
  return mojom::kSaveResponseMemoryAsWordDocumentToolName;
}

std::string_view SaveResponseMemoryAsWordDocumentTool::Description() const {
  return "Builds one Word (.docx) document from every response saved with "
         "save_response_to_memory so far this conversation - each as its "
         "own titled section, in the order they were saved - and saves it "
         "via a Save As dialog. Does not clear the session, so more "
         "responses can be saved and this called again. Fails with an "
         "error if nothing has been saved yet.";
}

std::optional<base::DictValue>
SaveResponseMemoryAsWordDocumentTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".docx extension is added automatically).")}});
}

std::optional<std::vector<std::string>>
SaveResponseMemoryAsWordDocumentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename};
}

void SaveResponseMemoryAsWordDocumentTool::UseTool(
    const std::string& input_json,
    UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no response memory session available."),
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
      base::BindOnce(&SaveResponseMemoryAsWordDocumentTool::OnSaveComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void SaveResponseMemoryAsWordDocumentTool::OnSaveComplete(
    UseToolCallback callback,
    bool success,
    std::string message) {
  std::move(callback).Run(CreateContentBlocksForText(message), {});
}

// ClearResponseMemoryTool --------------------------------------------------

ClearResponseMemoryTool::ClearResponseMemoryTool(
    ResponseMemorySession* session)
    : session_(session) {}

ClearResponseMemoryTool::~ClearResponseMemoryTool() = default;

std::string_view ClearResponseMemoryTool::Name() const {
  return mojom::kClearResponseMemoryToolName;
}

std::string_view ClearResponseMemoryTool::Description() const {
  return "Clears this conversation's response-memory session, discarding "
         "every response saved with save_response_to_memory so far, so the "
         "next one saved starts a fresh document.";
}

void ClearResponseMemoryTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  if (session_) {
    session_->Clear();
  }
  std::move(callback).Run(
      CreateContentBlocksForText("Cleared the response memory session."),
      {});
}

}  // namespace ai_chat

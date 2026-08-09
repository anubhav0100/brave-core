// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_RESPONSE_MEMORY_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_RESPONSE_MEMORY_TOOLS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace ai_chat {

class ResponseMemorySession;

// Saves a piece of the assistant's OWN generated text (an answer, a draft,
// a summary it just wrote) into this conversation's response-memory
// session, under a short label that becomes that entry's heading if the
// session is later saved as a Word document. Does not save anything to
// disk yet. Use this - not capture_page_to_session, which is for a page's
// real fetched content - whenever the user wants to keep something the
// assistant itself produced so several such responses can be merged into
// one document later via save_response_memory_as_word_document.
class SaveResponseToMemoryTool : public Tool {
 public:
  explicit SaveResponseToMemoryTool(ResponseMemorySession* session);
  ~SaveResponseToMemoryTool() override;

  SaveResponseToMemoryTool(const SaveResponseToMemoryTool&) = delete;
  SaveResponseToMemoryTool& operator=(const SaveResponseToMemoryTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<ResponseMemorySession> session_ = nullptr;

  base::WeakPtrFactory<SaveResponseToMemoryTool> weak_ptr_factory_{this};
};

// Builds one Word (.docx) document from every response saved with
// save_response_to_memory so far this conversation - each as its own
// titled section, in the order they were saved - and saves it via a Save
// As dialog. Does not clear the session, so more responses can be saved
// and this called again. Fails with an error if nothing has been saved yet.
class SaveResponseMemoryAsWordDocumentTool : public Tool {
 public:
  explicit SaveResponseMemoryAsWordDocumentTool(
      ResponseMemorySession* session);
  ~SaveResponseMemoryAsWordDocumentTool() override;

  SaveResponseMemoryAsWordDocumentTool(
      const SaveResponseMemoryAsWordDocumentTool&) = delete;
  SaveResponseMemoryAsWordDocumentTool& operator=(
      const SaveResponseMemoryAsWordDocumentTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnSaveComplete(UseToolCallback callback,
                      bool success,
                      std::string message);

  raw_ptr<ResponseMemorySession> session_ = nullptr;

  base::WeakPtrFactory<SaveResponseMemoryAsWordDocumentTool>
      weak_ptr_factory_{this};
};

// Clears this conversation's response-memory session, discarding every
// response saved with save_response_to_memory so far, so the next one
// saved starts a fresh document.
class ClearResponseMemoryTool : public Tool {
 public:
  explicit ClearResponseMemoryTool(ResponseMemorySession* session);
  ~ClearResponseMemoryTool() override;

  ClearResponseMemoryTool(const ClearResponseMemoryTool&) = delete;
  ClearResponseMemoryTool& operator=(const ClearResponseMemoryTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<ResponseMemorySession> session_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_RESPONSE_MEMORY_TOOLS_H_

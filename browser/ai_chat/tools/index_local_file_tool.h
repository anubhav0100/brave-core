// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_INDEX_LOCAL_FILE_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_INDEX_LOCAL_FILE_TOOL_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class TextFileExtractor;

// Asks the user to pick a local text-like file (.txt, .md, .csv, .json,
// .log, .html, .xml - not .docx, which read_word_document/
// create_word_document already own) via a native Open dialog, extracts its
// text via a hidden sandboxed WebContents (TextFileExtractor), and indexes
// it into this profile's on-device content index if enabled - the "index
// local files" item from the RAG design doc's deferred list. Distinct from
// read_word_document_tool: this covers arbitrary local files the user
// points at, not one specific document format the assistant also knows how
// to write back to.
class IndexLocalFileTool : public Tool {
 public:
  explicit IndexLocalFileTool(content::BrowserContext* browser_context);
  ~IndexLocalFileTool() override;

  IndexLocalFileTool(const IndexLocalFileTool&) = delete;
  IndexLocalFileTool& operator=(const IndexLocalFileTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnFileChosen(UseToolCallback callback, base::FilePath path);
  void OnTextExtracted(UseToolCallback callback,
                       std::string label,
                       std::optional<std::string> text);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<TextFileExtractor> extractor_;

  base::WeakPtrFactory<IndexLocalFileTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_INDEX_LOCAL_FILE_TOOL_H_

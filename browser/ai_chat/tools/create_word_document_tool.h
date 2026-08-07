// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_WORD_DOCUMENT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_WORD_DOCUMENT_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Exposes a tool that lets the assistant create a Word (.docx) document
// from a flat list of paragraphs (optionally marked as headings) and save
// it via a native Save As dialog anchored to the profile's active tab -
// picking the same filename as an existing document lets the user
// overwrite/update it in place. Builds a minimal OOXML package directly (no
// third-party office library exists in the tree) via document_download_util.h
// - see that file's header comment for the generation/save approach. V1
// scope is intentionally limited to plain text paragraphs with optional
// heading emphasis; no images, tables, or named styles.
class CreateWordDocumentTool : public Tool {
 public:
  explicit CreateWordDocumentTool(content::BrowserContext* browser_context);
  ~CreateWordDocumentTool() override;

  CreateWordDocumentTool(const CreateWordDocumentTool&) = delete;
  CreateWordDocumentTool& operator=(const CreateWordDocumentTool&) = delete;

  // Tool:
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnDownloadComplete(UseToolCallback callback,
                          std::string filename,
                          DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<CreateWordDocumentTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_WORD_DOCUMENT_TOOL_H_

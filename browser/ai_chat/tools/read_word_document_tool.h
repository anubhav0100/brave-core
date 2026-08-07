// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_READ_WORD_DOCUMENT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_READ_WORD_DOCUMENT_TOOL_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Lets the assistant read an existing Word (.docx) document's plain text
// into the conversation, so the user can ask it to edit/update a file they
// already have: read it with this tool, discuss the changes, then have the
// assistant call CreateWordDocumentTool with the full updated content and
// pick the same file in its Save As dialog to overwrite it in place.
//
// Always shows a native "Open" file dialog rather than accepting a path
// from the model - the assistant can only read a file the user explicitly
// picked, not an arbitrary path it guesses or remembers.
class ReadWordDocumentTool : public Tool {
 public:
  explicit ReadWordDocumentTool(content::BrowserContext* browser_context);
  ~ReadWordDocumentTool() override;

  ReadWordDocumentTool(const ReadWordDocumentTool&) = delete;
  ReadWordDocumentTool& operator=(const ReadWordDocumentTool&) = delete;

  // Tool:
  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnTextRead(UseToolCallback callback,
                  std::optional<std::string> text);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<ReadWordDocumentTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_READ_WORD_DOCUMENT_TOOL_H_

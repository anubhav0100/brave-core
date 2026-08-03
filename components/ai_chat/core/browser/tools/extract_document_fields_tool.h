// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_EXTRACT_DOCUMENT_FIELDS_TOOL_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_EXTRACT_DOCUMENT_FIELDS_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace ai_chat {

// Tool for recording a structured list of named fields extracted from the
// text of a document the user has uploaded (e.g. a PDF, whose text was
// already surfaced into the conversation as plain text - see
// browser/ai_chat/pdf_text_extractor.h, which does not itself produce any
// structure). The assistant does the actual extraction reasoning itself,
// using the document text already in its context, and calls this tool with
// the result; UseTool only validates the shape of what's provided and echoes
// it back as a clean checkpoint that a later tool call (e.g. one that maps
// these fields onto a web form) can refer back to by field name.
class ExtractDocumentFieldsTool : public Tool {
 public:
  ExtractDocumentFieldsTool();
  ~ExtractDocumentFieldsTool() override;

  ExtractDocumentFieldsTool(const ExtractDocumentFieldsTool&) = delete;
  ExtractDocumentFieldsTool& operator=(const ExtractDocumentFieldsTool&) =
      delete;

  // Tool overrides
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  bool SupportsConversation(bool is_temporary,
                            bool has_untrusted_content,
                            const ConversationCapabilitySet&
                                conversation_capabilities) const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_EXTRACT_DOCUMENT_FIELDS_TOOL_H_

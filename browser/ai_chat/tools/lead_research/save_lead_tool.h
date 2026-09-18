// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_SAVE_LEAD_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_SAVE_LEAD_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Records a company as a lead against a campaign, with the evidence for
// why it's relevant - see
// Brave_AI_Lead_Assistant_Development_Blueprint.md section 9.3 ("evidence
// envelope"). Purely local bookkeeping (no network/OS action). Only
// records facts actually observed - the tool description is the only
// guardrail against fabrication here, since this is the narrow
// browser-side slice without the blueprint's full evidence/provenance
// service.
class SaveLeadTool : public Tool {
 public:
  explicit SaveLeadTool(content::BrowserContext* browser_context);
  ~SaveLeadTool() override;

  SaveLeadTool(const SaveLeadTool&) = delete;
  SaveLeadTool& operator=(const SaveLeadTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_SAVE_LEAD_TOOL_H_

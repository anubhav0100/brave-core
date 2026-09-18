// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_LIST_LEADS_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_LIST_LEADS_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Lists the leads saved so far for a campaign, so the assistant can review
// what's been recorded before adding more or reporting back to the user -
// see Brave_AI_Lead_Assistant_Development_Blueprint.md section 4, "Step
// 7".
class ListLeadsTool : public Tool {
 public:
  explicit ListLeadsTool(content::BrowserContext* browser_context);
  ~ListLeadsTool() override;

  ListLeadsTool(const ListLeadsTool&) = delete;
  ListLeadsTool& operator=(const ListLeadsTool&) = delete;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_LIST_LEADS_TOOL_H_

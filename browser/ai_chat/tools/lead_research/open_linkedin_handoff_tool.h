// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_LINKEDIN_HANDOFF_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_LINKEDIN_HANDOFF_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Opens LinkedIn's own website and hands the decision-maker search off to
// the user - see Brave_AI_Lead_Assistant_Development_Blueprint.md section
// 8.1. Brave has no LinkedIn API access or scraping capability here by
// design: LinkedIn's User Agreement restricts automated access, so this
// tool only opens linkedin.com and suggests search terms for the user to
// type into LinkedIn's own native search themselves. It never reads,
// automates, or extracts anything from LinkedIn's pages.
class OpenLinkedInHandoffTool : public Tool {
 public:
  explicit OpenLinkedInHandoffTool(content::BrowserContext* browser_context);
  ~OpenLinkedInHandoffTool() override;

  OpenLinkedInHandoffTool(const OpenLinkedInHandoffTool&) = delete;
  OpenLinkedInHandoffTool& operator=(const OpenLinkedInHandoffTool&) = delete;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_LINKEDIN_HANDOFF_TOOL_H_

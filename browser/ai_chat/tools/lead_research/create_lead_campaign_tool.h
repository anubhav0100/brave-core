// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CREATE_LEAD_CAMPAIGN_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CREATE_LEAD_CAMPAIGN_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Creates a new lead-research campaign record - see
// Brave_AI_Lead_Assistant_Development_Blueprint.md section 4, "Step 2".
// Purely local bookkeeping: it does not search, contact, or fetch anything
// itself. Use open_maps_search/open_linkedin_handoff to actually look for
// companies, and save_lead to record ones found, against the campaign id
// this returns.
class CreateLeadCampaignTool : public Tool {
 public:
  explicit CreateLeadCampaignTool(content::BrowserContext* browser_context);
  ~CreateLeadCampaignTool() override;

  CreateLeadCampaignTool(const CreateLeadCampaignTool&) = delete;
  CreateLeadCampaignTool& operator=(const CreateLeadCampaignTool&) = delete;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CREATE_LEAD_CAMPAIGN_TOOL_H_

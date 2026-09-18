// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/list_leads_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/lead_research/lead_research_state.h"
#include "brave/browser/lead_research/lead_research_state_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyCampaignId[] = "campaign_id";
}  // namespace

ListLeadsTool::ListLeadsTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ListLeadsTool::~ListLeadsTool() = default;

std::string_view ListLeadsTool::Name() const {
  return mojom::kListLeadsToolName;
}

std::string_view ListLeadsTool::Description() const {
  return "Lists the leads saved so far for a campaign - use this to "
         "review what's been recorded before adding more, or before "
         "reporting a shortlist back to the user.";
}

std::optional<base::DictValue> ListLeadsTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyCampaignId,
       StringProperty("The campaign id to list leads for")},
  });
}

std::optional<std::vector<std::string>> ListLeadsTool::RequiredProperties()
    const {
  return std::optional<std::vector<std::string>>({kPropertyCampaignId});
}

void ListLeadsTool::UseTool(const std::string& input_json,
                           UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* campaign_id =
      input ? input->FindString(kPropertyCampaignId) : nullptr;
  if (!campaign_id || campaign_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'campaign_id'"), {});
    return;
  }

  auto* state = lead_research::LeadResearchStateFactory::GetForBrowserContext(
      browser_context_);
  if (!state->GetCampaign(*campaign_id)) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: no campaign found with id '",
                          *campaign_id, "'."})),
        {});
    return;
  }

  std::vector<lead_research::LeadResearchState::Lead> leads =
      state->GetLeads(*campaign_id);
  if (leads.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"No leads saved yet for campaign ", *campaign_id, "."})),
        {});
    return;
  }

  std::string text = base::StrCat(
      {base::NumberToString(leads.size()), " lead(s) for campaign ",
       *campaign_id, ":\n"});
  for (const lead_research::LeadResearchState::Lead& lead : leads) {
    std::string score_suffix =
        lead.score >= 0
            ? base::StrCat({" - score: ", base::NumberToString(lead.score),
                           "/100 (", base::NumberToString(
                                         lead.evidence_coverage_percent),
                           "% covered)"})
            : " - not yet scored";
    text += base::StrCat(
        {"- ", lead.name, lead.domain.empty() ? "" : base::StrCat({" (", lead.domain, ")"}),
         lead.city.empty() ? "" : base::StrCat({", ", lead.city}),
         lead.service_fit.empty() ? ""
                                  : base::StrCat({" - fit: ", lead.service_fit}),
         score_suffix, " [", lead.stage, "] id=", lead.id, "\n  notes: ",
         lead.notes, "\n"});
  }

  std::move(callback).Run(CreateContentBlocksForText(text), {});
}

}  // namespace ai_chat

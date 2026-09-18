// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/save_lead_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/lead_research/lead_research_state.h"
#include "brave/browser/lead_research/lead_research_state_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyCampaignId[] = "campaign_id";
constexpr char kPropertyName[] = "name";
constexpr char kPropertyDomain[] = "domain";
constexpr char kPropertyCity[] = "city";
constexpr char kPropertyServiceFit[] = "service_fit";
constexpr char kPropertyNotes[] = "notes";
}  // namespace

SaveLeadTool::SaveLeadTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

SaveLeadTool::~SaveLeadTool() = default;

std::string_view SaveLeadTool::Name() const {
  return mojom::kSaveLeadToolName;
}

std::string_view SaveLeadTool::Description() const {
  return "Records a company as a lead for a campaign, with the evidence/"
         "reasoning for why it's relevant. Only record companies and "
         "facts actually observed - from a page you were shown, a Maps/"
         "LinkedIn result the user reported, or something the user told "
         "you directly. Never invent a company, domain, or contact detail "
         "that wasn't actually observed; leave a field empty rather than "
         "guessing.";
}

std::optional<base::DictValue> SaveLeadTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyCampaignId,
       StringProperty("The campaign id this lead belongs to, from "
                      "create_lead_campaign")},
      {kPropertyName, StringProperty("The company's name")},
      {kPropertyDomain,
       StringProperty("The company's official website domain, if known "
                      "(optional)")},
      {kPropertyCity,
       StringProperty("City/location of this company or the relevant "
                      "plant/office (optional)")},
      {kPropertyServiceFit,
       StringProperty("Which offered service is relevant to this company "
                      "(optional)")},
      {kPropertyNotes,
       StringProperty("Evidence/reasoning for why this company is "
                      "relevant - what was actually observed")},
  });
}

std::optional<std::vector<std::string>> SaveLeadTool::RequiredProperties()
    const {
  return std::optional<std::vector<std::string>>(
      {kPropertyCampaignId, kPropertyName, kPropertyNotes});
}

void SaveLeadTool::UseTool(const std::string& input_json,
                          UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* campaign_id =
      input ? input->FindString(kPropertyCampaignId) : nullptr;
  const std::string* name = input ? input->FindString(kPropertyName) : nullptr;
  const std::string* notes =
      input ? input->FindString(kPropertyNotes) : nullptr;
  if (!campaign_id || campaign_id->empty() || !name || name->empty() ||
      !notes || notes->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing 'campaign_id', 'name' or 'notes'"),
        {});
    return;
  }

  auto* state = lead_research::LeadResearchStateFactory::GetForBrowserContext(
      browser_context_);
  if (!state->GetCampaign(*campaign_id)) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: no campaign found with id '",
                          *campaign_id,
                          "' - call create_lead_campaign first."})),
        {});
    return;
  }

  const std::string* domain_ptr =
      input ? input->FindString(kPropertyDomain) : nullptr;
  const std::string* city_ptr =
      input ? input->FindString(kPropertyCity) : nullptr;
  const std::string* service_fit_ptr =
      input ? input->FindString(kPropertyServiceFit) : nullptr;

  std::string lead_id = state->SaveLead(
      *campaign_id, *name, domain_ptr ? *domain_ptr : std::string(),
      city_ptr ? *city_ptr : std::string(),
      service_fit_ptr ? *service_fit_ptr : std::string(), *notes);

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Saved lead ", lead_id, " (", *name, ") to campaign ",
           *campaign_id, "."})),
      {});
}

}  // namespace ai_chat

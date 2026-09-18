// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/create_lead_campaign_tool.h"

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
constexpr char kPropertyGoal[] = "goal";
constexpr char kPropertyLocations[] = "locations";
constexpr char kPropertyIndustries[] = "industries";
constexpr char kPropertyServices[] = "services";
constexpr char kPropertyExclusions[] = "exclusions";
constexpr char kPropertyTargetCount[] = "target_count";

std::vector<std::string> ReadStringArray(const base::DictValue* input,
                                         const char* key) {
  std::vector<std::string> result;
  const base::ListValue* list = input ? input->FindList(key) : nullptr;
  if (!list) {
    return result;
  }
  for (const base::Value& item : *list) {
    if (item.is_string()) {
      result.push_back(item.GetString());
    }
  }
  return result;
}
}  // namespace

CreateLeadCampaignTool::CreateLeadCampaignTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreateLeadCampaignTool::~CreateLeadCampaignTool() = default;

std::string_view CreateLeadCampaignTool::Name() const {
  return mojom::kCreateLeadCampaignToolName;
}

std::string_view CreateLeadCampaignTool::Description() const {
  return "Creates a lead-research campaign record from a business "
         "development request (e.g. \"find manufacturers in Faridabad "
         "that could use website development or SEO\"). Extracts target "
         "locations, industries, services to pitch, and exclusions from "
         "the request. This only creates the campaign record - it does "
         "not search for or contact anyone. After creating a campaign, "
         "use open_maps_search and open_linkedin_handoff to actually look "
         "for companies (in tabs the user can see), and save_lead to "
         "record ones you or the user identify. Never invent a company or "
         "fact - only record what was actually found.";
}

std::optional<base::DictValue> CreateLeadCampaignTool::InputProperties()
    const {
  return CreateInputProperties({
      {kPropertyGoal,
       StringProperty("The original business development request, "
                      "verbatim or lightly summarized")},
      {kPropertyLocations,
       ArrayProperty("Target locations/cities to focus on",
                     StringProperty("A location"))},
      {kPropertyIndustries,
       ArrayProperty("Target industries/sectors to focus on",
                     StringProperty("An industry"))},
      {kPropertyServices,
       ArrayProperty("Services being pitched to these companies",
                     StringProperty("A service"))},
      {kPropertyExclusions,
       ArrayProperty("Companies, sectors or company types to exclude "
                     "(optional)",
                     StringProperty("An exclusion"))},
      {kPropertyTargetCount,
       IntegerProperty("Roughly how many companies to aim for (optional, "
                       "default is left unset/unbounded)")},
  });
}

std::optional<std::vector<std::string>>
CreateLeadCampaignTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyGoal});
}

void CreateLeadCampaignTool::UseTool(const std::string& input_json,
                                     UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* goal = input ? input->FindString(kPropertyGoal) : nullptr;
  if (!goal || goal->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'goal'"), {});
    return;
  }

  const base::DictValue* dict = input ? &*input : nullptr;
  std::vector<std::string> locations =
      ReadStringArray(dict, kPropertyLocations);
  std::vector<std::string> industries =
      ReadStringArray(dict, kPropertyIndustries);
  std::vector<std::string> services = ReadStringArray(dict, kPropertyServices);
  std::vector<std::string> exclusions =
      ReadStringArray(dict, kPropertyExclusions);
  int target_count =
      input ? input->FindInt(kPropertyTargetCount).value_or(0) : 0;

  auto* state = lead_research::LeadResearchStateFactory::GetForBrowserContext(
      browser_context_);
  std::string campaign_id = state->CreateCampaign(
      *goal, std::move(locations), std::move(industries),
      std::move(services), std::move(exclusions), target_count);

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Created campaign ", campaign_id, ". Use open_maps_search and "
           "open_linkedin_handoff to research companies, then save_lead to "
           "record the ones that fit."})),
      {});
}

}  // namespace ai_chat

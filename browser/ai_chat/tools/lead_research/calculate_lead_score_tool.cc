// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/calculate_lead_score_tool.h"

#include <algorithm>
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
constexpr char kPropertyLeadId[] = "lead_id";
constexpr char kPropertyProfileFit[] = "profile_fit_score";
constexpr char kPropertyServiceOpportunity[] = "service_opportunity_score";
constexpr char kPropertyDemandSignal[] = "demand_signal_score";
constexpr char kPropertyContactability[] = "contactability_score";
constexpr char kPropertyEvidenceQuality[] = "evidence_quality_score";
constexpr char kPropertyRationale[] = "rationale";

struct Dimension {
  const char* property;
  int max_points;
};

// Matches Brave_AI_Lead_Assistant_Development_Blueprint.md section 11.1's
// rubric exactly - weights sum to 100.
constexpr Dimension kDimensions[] = {
    {kPropertyProfileFit, 25},
    {kPropertyServiceOpportunity, 35},
    {kPropertyDemandSignal, 20},
    {kPropertyContactability, 10},
    {kPropertyEvidenceQuality, 10},
};

// Below this coverage, too much of the rubric is unsupported for the
// total score to mean much - blueprint 11.1's "research incomplete"
// state.
constexpr int kMinCoveragePercentForLabel = 60;
}  // namespace

CalculateLeadScoreTool::CalculateLeadScoreTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CalculateLeadScoreTool::~CalculateLeadScoreTool() = default;

std::string_view CalculateLeadScoreTool::Name() const {
  return mojom::kCalculateLeadScoreToolName;
}

std::string_view CalculateLeadScoreTool::Description() const {
  return "Scores a saved lead using a fixed weighted rubric: profile fit "
         "(/25), service opportunity (/35), demand signal (/20), "
         "contactability (/10), evidence quality (/10). Only supply a "
         "sub-score for a dimension you can actually ground in evidence "
         "you observed - omit dimensions you don't have evidence for "
         "rather than guessing; the tool tracks how much of the rubric "
         "was actually covered, so missing information lowers coverage, "
         "not the company's fairness as a prospect. A high score means "
         "'worth researching further,' not 'confirmed buyer.'";
}

std::optional<base::DictValue> CalculateLeadScoreTool::InputProperties()
    const {
  return CreateInputProperties({
      {kPropertyLeadId, StringProperty("The lead id to score, from save_lead")},
      {kPropertyProfileFit,
       IntegerProperty("0-25: sector/location/size fit with the target "
                       "customer profile (omit if unsupported)")},
      {kPropertyServiceOpportunity,
       IntegerProperty("0-35: specific evidence of an opportunity for the "
                       "offered service (omit if unsupported)")},
      {kPropertyDemandSignal,
       IntegerProperty("0-20: a dated requirement, tender, initiative or "
                       "explicit request (omit if unsupported)")},
      {kPropertyContactability,
       IntegerProperty("0-10: a suitable public business route or "
                       "authorized contact (omit if unsupported)")},
      {kPropertyEvidenceQuality,
       IntegerProperty("0-10: how verifiable/fresh the evidence is (omit "
                       "if unsupported)")},
      {kPropertyRationale,
       StringProperty("Brief explanation grounding the supplied "
                      "sub-scores in what was actually observed")},
  });
}

std::optional<std::vector<std::string>>
CalculateLeadScoreTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>(
      {kPropertyLeadId, kPropertyRationale});
}

void CalculateLeadScoreTool::UseTool(const std::string& input_json,
                                     UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* lead_id =
      input ? input->FindString(kPropertyLeadId) : nullptr;
  const std::string* rationale =
      input ? input->FindString(kPropertyRationale) : nullptr;
  if (!lead_id || lead_id->empty() || !rationale || rationale->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'lead_id' or 'rationale'"),
        {});
    return;
  }

  int supported_score = 0;
  int covered_weight = 0;
  for (const Dimension& dimension : kDimensions) {
    std::optional<int> value =
        input ? input->FindInt(dimension.property) : std::nullopt;
    if (!value) {
      continue;
    }
    supported_score += std::clamp(*value, 0, dimension.max_points);
    covered_weight += dimension.max_points;
  }

  auto* state = lead_research::LeadResearchStateFactory::GetForBrowserContext(
      browser_context_);
  if (!state->UpdateLeadScore(*lead_id, supported_score, covered_weight,
                              *rationale)) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: no lead found with id '", *lead_id, "'."})),
        {});
    return;
  }

  std::string label;
  if (covered_weight < kMinCoveragePercentForLabel) {
    label = "Research incomplete";
  } else if (supported_score >= 75) {
    label = "High priority";
  } else if (supported_score >= 50) {
    label = "Medium priority";
  } else {
    label = "Low priority";
  }

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Scored lead ", *lead_id, ": ", base::NumberToString(supported_score),
           "/100 (", base::NumberToString(covered_weight),
           "% of the rubric covered) - ", label, "."})),
      {});
}

}  // namespace ai_chat

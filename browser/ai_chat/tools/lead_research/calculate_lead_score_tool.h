// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CALCULATE_LEAD_SCORE_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CALCULATE_LEAD_SCORE_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Computes a deterministic priority score for a saved lead, using the
// weighted rubric from
// Brave_AI_Lead_Assistant_Development_Blueprint.md section 11.1 (profile
// fit /25, service opportunity /35, demand signal /20, contactability
// /10, evidence quality /10). The model supplies a sub-score for each
// dimension it can actually support with evidence, and omits ones it
// can't - this tool sums only the supplied dimensions (never
// renormalizing a partial score up to 100) and separately reports what
// share of the rubric was actually covered, so a low score from missing
// information reads as "research incomplete" rather than "poor
// prospect." Purely local arithmetic; it does not itself decide what the
// sub-scores should be - the model's own judgment, grounded in what it
// actually observed, does.
class CalculateLeadScoreTool : public Tool {
 public:
  explicit CalculateLeadScoreTool(content::BrowserContext* browser_context);
  ~CalculateLeadScoreTool() override;

  CalculateLeadScoreTool(const CalculateLeadScoreTool&) = delete;
  CalculateLeadScoreTool& operator=(const CalculateLeadScoreTool&) = delete;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_CALCULATE_LEAD_SCORE_TOOL_H_

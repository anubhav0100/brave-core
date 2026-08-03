// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_PROPOSE_FORM_FILL_PLAN_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_PROPOSE_FORM_FILL_PLAN_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace ai_chat {

// Tool for proposing a plan to fill fields on the current web page (e.g. a
// form) from data the assistant already has - typically fields previously
// recorded with ExtractDocumentFieldsTool. This tool does not touch the page
// itself: it only validates the shape of the proposed plan and, via
// RequiresUserInteractionBeforeHandling, blocks on user confirmation of the
// plan's human-readable description before letting the assistant proceed.
// Once approved, the assistant is expected to carry out the plan by calling
// the existing per-field tools (click_element / type_text /
// select_dropdown) for each entry - this tool intentionally does not
// perform those actions itself, so it has no need for a
// ContentAgentTaskProvider dependency the way Click/Type/SelectTool do.
class ProposeFormFillPlanTool : public Tool {
 public:
  ProposeFormFillPlanTool();
  ~ProposeFormFillPlanTool() override;

  ProposeFormFillPlanTool(const ProposeFormFillPlanTool&) = delete;
  ProposeFormFillPlanTool& operator=(const ProposeFormFillPlanTool&) = delete;

  // Tool overrides
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  bool IsAgentTool() const override;
  std::variant<bool, mojom::PermissionChallengePtr>
  RequiresUserInteractionBeforeHandling(
      const mojom::ToolUseEvent& tool_use) const override;
  void UserPermissionGranted(const std::string& tool_use_id) override;
  bool SupportsConversation(bool is_temporary,
                            bool has_untrusted_content,
                            const ConversationCapabilitySet&
                                conversation_capabilities) const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  bool user_has_granted_permission_ = false;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_PROPOSE_FORM_FILL_PLAN_TOOL_H_

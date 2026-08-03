// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/propose_form_fill_plan_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ai_chat/tools/target_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNamePlan[] = "plan";
constexpr char kPropertyNameEntries[] = "entries";
constexpr char kEntryPropertyNameTarget[] = "target";
constexpr char kEntryPropertyNameSourceField[] = "source_field";
constexpr char kEntryPropertyNameValue[] = "value";

}  // namespace

ProposeFormFillPlanTool::ProposeFormFillPlanTool() = default;

ProposeFormFillPlanTool::~ProposeFormFillPlanTool() = default;

std::string_view ProposeFormFillPlanTool::Name() const {
  return mojom::kProposeFormFillPlanToolName;
}

std::string_view ProposeFormFillPlanTool::Description() const {
  return "Propose a plan to fill fields on the current page (e.g. a form) "
         "using data you already have, such as fields previously recorded "
         "with the extract_document_fields tool. Include a clear 'plan' "
         "description for the user to confirm before anything is filled. "
         "After this plan is approved, carry it out yourself by calling "
         "type_text / select_dropdown / click_element for each entry - this "
         "tool only records and confirms the plan, it does not fill "
         "anything in.";
}

std::optional<base::DictValue> ProposeFormFillPlanTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNamePlan,
        StringProperty("Human-readable summary of which fields will be "
                       "filled with which values, shown to the user for "
                       "confirmation before anything is filled in")},
       {kPropertyNameEntries,
        ArrayProperty(
            "The proposed field-to-target mappings",
            ObjectProperty(
                "A single field mapping",
                {{kEntryPropertyNameTarget,
                  target_util::TargetProperty(
                      "The form field element to fill")},
                 {kEntryPropertyNameSourceField,
                  StringProperty("Name of the previously-extracted "
                                 "document field this value comes from")},
                 {kEntryPropertyNameValue,
                  StringProperty(
                      "The value to enter into this field")}}))}});
}

std::optional<std::vector<std::string>>
ProposeFormFillPlanTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNamePlan, kPropertyNameEntries};
}

bool ProposeFormFillPlanTool::IsAgentTool() const {
  return true;
}

std::variant<bool, mojom::PermissionChallengePtr>
ProposeFormFillPlanTool::RequiresUserInteractionBeforeHandling(
    const mojom::ToolUseEvent& tool_use) const {
  if (user_has_granted_permission_) {
    return false;
  }

  // Provide a PermissionChallenge only if input is valid and a plan was
  // provided. If it isn't valid, it will be rejected by UseTool, giving the
  // assistant a chance to correct its call.
  auto input = base::JSONReader::ReadDict(tool_use.arguments_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    return false;
  }

  const auto* plan = input->FindString(kPropertyNamePlan);
  if (!plan || plan->empty()) {
    return false;
  }

  return mojom::PermissionChallenge::New(std::nullopt, *plan);
}

void ProposeFormFillPlanTool::UserPermissionGranted(
    const std::string& tool_use_id) {
  user_has_granted_permission_ = true;
}

bool ProposeFormFillPlanTool::SupportsConversation(
    bool is_temporary,
    bool has_untrusted_content,
    const ConversationCapabilitySet& conversation_capabilities) const {
  return conversation_capabilities.contains(
      mojom::ConversationCapability::CONTENT_AGENT);
}

void ProposeFormFillPlanTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: Invalid JSON input, input must be a JSON object"),
        {});
    return;
  }

  const std::string* plan = input->FindString(kPropertyNamePlan);
  if (!plan || plan->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Missing or empty 'plan' field"),
        {});
    return;
  }

  const base::ListValue* entries = input->FindList(kPropertyNameEntries);
  if (!entries || entries->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Missing or empty 'entries' array"),
        {});
    return;
  }

  size_t valid_count = 0;
  for (const auto& entry_value : *entries) {
    const base::DictValue* entry_dict = entry_value.GetIfDict();
    if (!entry_dict) {
      std::move(callback).Run(
          CreateContentBlocksForText(
              "Error: Each entry in 'entries' must be an object"),
          {});
      return;
    }

    const base::DictValue* target_dict =
        entry_dict->FindDict(kEntryPropertyNameTarget);
    if (!target_dict) {
      std::move(callback).Run(
          CreateContentBlocksForText(base::StrCat(
              {"Error: Entry at index ", base::NumberToString(valid_count),
               " is missing a 'target'"})),
          {});
      return;
    }

    auto target = target_util::ParseTargetInput(*target_dict);
    if (!target.has_value()) {
      std::move(callback).Run(
          CreateContentBlocksForText(base::StrCat(
              {"Error: Entry at index ", base::NumberToString(valid_count),
               " has an invalid 'target': ", target.error()})),
          {});
      return;
    }

    const std::string* value =
        entry_dict->FindString(kEntryPropertyNameValue);
    if (!value) {
      std::move(callback).Run(
          CreateContentBlocksForText(base::StrCat(
              {"Error: Entry at index ", base::NumberToString(valid_count),
               " is missing a 'value'"})),
          {});
      return;
    }

    ++valid_count;
  }

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Fill plan approved by the user: ",
           base::NumberToString(valid_count),
           " field(s) ready to be filled. Proceed now by calling "
           "type_text / select_dropdown / click_element for each entry's "
           "target."})),
      {});
}

}  // namespace ai_chat

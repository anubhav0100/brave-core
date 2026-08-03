// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/propose_form_fill_plan_tool.h"

#include <memory>
#include <string>
#include <variant>

#include "base/strings/strcat.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

constexpr char kValidEntry[] =
    R"({"target": {"document_identifier": "doc1", "content_node_id": 5},
        "source_field": "vendor_name", "value": "Acme Corp"})";

std::string RunTool(ProposeFormFillPlanTool* tool, const std::string& json) {
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool->UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

mojom::ToolUseEventPtr CreateToolUseEvent(const std::string& json) {
  return mojom::ToolUseEvent::New(mojom::kProposeFormFillPlanToolName, "1",
                                  json, std::nullopt, std::nullopt, nullptr,
                                  false);
}

}  // namespace

class ProposeFormFillPlanToolTest : public testing::Test {
 public:
  void SetUp() override {
    tool_ = std::make_unique<ProposeFormFillPlanTool>();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<ProposeFormFillPlanTool> tool_;
};

TEST_F(ProposeFormFillPlanToolTest, UseTool_ValidInput) {
  const std::string input_json = base::StrCat(
      {R"({"plan": "Fill vendor and amount", "entries": [)", kValidEntry,
       "]}"});

  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Fill plan approved by the user: 1 field(s) ready to be filled. "
            "Proceed now by calling type_text / select_dropdown / "
            "click_element for each entry's target.");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunTool(tool_.get(), "not json"),
            "Error: Invalid JSON input, input must be a JSON object");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_MissingPlan) {
  const std::string input_json =
      base::StrCat({R"({"entries": [)", kValidEntry, "]}"});
  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Error: Missing or empty 'plan' field");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_MissingEntries) {
  EXPECT_EQ(RunTool(tool_.get(), R"({"plan": "Fill the form"})"),
            "Error: Missing or empty 'entries' array");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_EntryNotAnObject) {
  EXPECT_EQ(
      RunTool(tool_.get(), R"({"plan": "Fill the form", "entries": ["x"]})"),
      "Error: Each entry in 'entries' must be an object");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_EntryMissingTarget) {
  const std::string input_json =
      R"({"plan": "Fill the form",
          "entries": [{"source_field": "vendor_name", "value": "Acme"}]})";
  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Error: Entry at index 0 is missing a 'target'");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_EntryInvalidTarget) {
  const std::string input_json =
      R"({"plan": "Fill the form",
          "entries": [{"target": {}, "source_field": "vendor_name",
                       "value": "Acme"}]})";
  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Error: Entry at index 0 has an invalid 'target': "
            "Error: Target must contain one of either 'x' and 'y' or "
            "'document_identifier' and optional 'content_node_id'");
}

TEST_F(ProposeFormFillPlanToolTest, UseTool_EntryMissingValue) {
  const std::string input_json = base::StrCat(
      {R"({"plan": "Fill the form", "entries": [{"target": )",
       R"({"document_identifier": "doc1"}, "source_field": "vendor_name"}]})"});
  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Error: Entry at index 0 is missing a 'value'");
}

TEST_F(ProposeFormFillPlanToolTest, RequiresUserInteractionBeforeHandling_WithPlan) {
  auto tool_use = CreateToolUseEvent(
      R"({"plan": "Fill vendor and amount", "entries": []})");
  std::variant<bool, mojom::PermissionChallengePtr> result =
      tool_->RequiresUserInteractionBeforeHandling(*tool_use);
  ASSERT_TRUE(std::holds_alternative<mojom::PermissionChallengePtr>(result));
  EXPECT_EQ(std::get<mojom::PermissionChallengePtr>(result)->plan,
            "Fill vendor and amount");
}

TEST_F(ProposeFormFillPlanToolTest,
       RequiresUserInteractionBeforeHandling_WithoutPlan) {
  auto tool_use = CreateToolUseEvent(R"({"entries": []})");
  std::variant<bool, mojom::PermissionChallengePtr> result =
      tool_->RequiresUserInteractionBeforeHandling(*tool_use);
  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_FALSE(std::get<bool>(result));
}

TEST_F(ProposeFormFillPlanToolTest,
       RequiresUserInteractionBeforeHandling_InvalidJson) {
  auto tool_use = CreateToolUseEvent("not json");
  std::variant<bool, mojom::PermissionChallengePtr> result =
      tool_->RequiresUserInteractionBeforeHandling(*tool_use);
  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_FALSE(std::get<bool>(result));
}

TEST_F(ProposeFormFillPlanToolTest,
       RequiresUserInteractionBeforeHandling_AfterPermissionGranted) {
  auto tool_use = CreateToolUseEvent(R"({"plan": "Fill the form"})");
  tool_->UserPermissionGranted("1");
  std::variant<bool, mojom::PermissionChallengePtr> result =
      tool_->RequiresUserInteractionBeforeHandling(*tool_use);
  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_FALSE(std::get<bool>(result));
}

TEST_F(ProposeFormFillPlanToolTest, IsAgentTool_ReturnsTrue) {
  EXPECT_TRUE(tool_->IsAgentTool());
}

TEST_F(ProposeFormFillPlanToolTest, SupportsConversation_ContentAgent) {
  EXPECT_TRUE(tool_->SupportsConversation(
      /*is_temporary=*/false, /*has_untrusted_content=*/false,
      {mojom::ConversationCapability::CONTENT_AGENT}));
}

TEST_F(ProposeFormFillPlanToolTest, SupportsConversation_ChatOnly) {
  EXPECT_FALSE(tool_->SupportsConversation(
      /*is_temporary=*/false, /*has_untrusted_content=*/false,
      {mojom::ConversationCapability::CHAT}));
}

}  // namespace ai_chat

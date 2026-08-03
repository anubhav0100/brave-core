// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/tools/extract_document_fields_tool.h"

#include <memory>
#include <string>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

std::string RunTool(ExtractDocumentFieldsTool* tool, const std::string& json) {
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool->UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

}  // namespace

class ExtractDocumentFieldsToolTest : public testing::Test {
 public:
  void SetUp() override {
    tool_ = std::make_unique<ExtractDocumentFieldsTool>();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<ExtractDocumentFieldsTool> tool_;
};

TEST_F(ExtractDocumentFieldsToolTest, UseTool_ValidInput) {
  const std::string input_json = R"({
    "document_type": "invoice",
    "fields": [
      {"name": "vendor_name", "value": "Acme Corp", "confidence": "high"},
      {"name": "total_amount", "value": "123.45", "confidence": "high"}
    ]
  })";

  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Recorded 2 structured field(s) from the document. You can now "
            "reference these fields by name in a subsequent tool call, such "
            "as one that maps them onto a web form.");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunTool(tool_.get(), "not json"),
            "Error: Invalid JSON input, input must be a JSON object");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_MissingFieldsArray) {
  EXPECT_EQ(RunTool(tool_.get(), R"({"document_type": "invoice"})"),
            "Error: Missing or empty 'fields' array");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_EmptyFieldsArray) {
  EXPECT_EQ(RunTool(tool_.get(), R"({"fields": []})"),
            "Error: Missing or empty 'fields' array");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_FieldsEntryNotAnObject) {
  EXPECT_EQ(RunTool(tool_.get(), R"({"fields": ["oops"]})"),
            "Error: Each entry in 'fields' must be an object");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_FieldMissingName) {
  EXPECT_EQ(RunTool(tool_.get(), R"({"fields": [{"value": "123.45"}]})"),
            "Error: Field at index 0 is missing a non-empty 'name' or a "
            "'value'");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_FieldMissingValue) {
  EXPECT_EQ(RunTool(tool_.get(),
                    R"({"fields": [{"name": "vendor_name"}]})"),
            "Error: Field at index 0 is missing a non-empty 'name' or a "
            "'value'");
}

TEST_F(ExtractDocumentFieldsToolTest, UseTool_SecondFieldInvalid) {
  const std::string input_json = R"({
    "fields": [
      {"name": "vendor_name", "value": "Acme Corp"},
      {"name": "", "value": "123.45"}
    ]
  })";
  EXPECT_EQ(RunTool(tool_.get(), input_json),
            "Error: Field at index 1 is missing a non-empty 'name' or a "
            "'value'");
}

TEST_F(ExtractDocumentFieldsToolTest, SupportsConversation_ContentAgent) {
  EXPECT_TRUE(tool_->SupportsConversation(
      /*is_temporary=*/false, /*has_untrusted_content=*/false,
      {mojom::ConversationCapability::CONTENT_AGENT}));
}

TEST_F(ExtractDocumentFieldsToolTest, SupportsConversation_ChatOnly) {
  EXPECT_FALSE(tool_->SupportsConversation(
      /*is_temporary=*/false, /*has_untrusted_content=*/false,
      {mojom::ConversationCapability::CHAT}));
}

}  // namespace ai_chat

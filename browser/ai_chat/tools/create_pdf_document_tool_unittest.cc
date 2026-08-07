// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_pdf_document_tool.h"

#include <string>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

// Only exercises input-validation failure paths, which return before
// spawning a PdfDocumentGenerator (and therefore before touching
// BrowserContext) - see the equivalent comment in
// create_word_document_tool_unittest.cc for why a real
// BrowserContext/WebContents/DownloadManager isn't set up here.
std::string RunToolExpectingValidationError(const std::string& json) {
  CreatePdfDocumentTool tool(/*browser_context=*/nullptr);
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool.UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

}  // namespace

class CreatePdfDocumentToolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CreatePdfDocumentToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunToolExpectingValidationError("not json"),
            "Error: failed to parse input JSON");
}

TEST_F(CreatePdfDocumentToolTest, UseTool_MissingFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"html_content": "<p>hi</p>"})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreatePdfDocumentToolTest, UseTool_EmptyFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"filename": "", "html_content": "<p>hi</p>"})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreatePdfDocumentToolTest, UseTool_MissingHtmlContent) {
  EXPECT_EQ(RunToolExpectingValidationError(R"({"filename": "report"})"),
            "Error: missing or empty 'html_content'");
}

TEST_F(CreatePdfDocumentToolTest, UseTool_EmptyHtmlContent) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"filename": "report", "html_content": ""})"),
            "Error: missing or empty 'html_content'");
}

}  // namespace ai_chat

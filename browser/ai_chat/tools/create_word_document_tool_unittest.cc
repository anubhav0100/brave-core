// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_word_document_tool.h"

#include <memory>
#include <string>

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

// Only exercises input-validation failure paths, which return before
// touching the BrowserContext - safe to pass nullptr here. The happy path
// (real file generation + download) is covered by the manual end-to-end
// smoke test described in the implementation plan, since exercising a real
// content::DownloadManager needs a heavier content-layer test harness than
// this pure input-validation logic warrants.
std::string RunToolExpectingValidationError(const std::string& json) {
  CreateWordDocumentTool tool(/*browser_context=*/nullptr);
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool.UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

}  // namespace

class CreateWordDocumentToolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CreateWordDocumentToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunToolExpectingValidationError("not json"),
            "Error: failed to parse input JSON");
}

TEST_F(CreateWordDocumentToolTest, UseTool_MissingFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"paragraphs": [{"text": "hello"}]})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreateWordDocumentToolTest, UseTool_EmptyFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"filename": "", "paragraphs": [{"text": "hello"}]})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreateWordDocumentToolTest, UseTool_MissingParagraphs) {
  EXPECT_EQ(RunToolExpectingValidationError(R"({"filename": "report"})"),
            "Error: missing or empty 'paragraphs' array");
}

TEST_F(CreateWordDocumentToolTest, UseTool_EmptyParagraphs) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"filename": "report", "paragraphs": []})"),
            "Error: missing or empty 'paragraphs' array");
}

// Exercises the pure XML-generation logic directly. Lives in
// document_download_util.h/.cc (shared with any future feature that
// accumulates paragraphs over time, not just this single-call tool).
class BuildWordDocumentXmlTest : public testing::Test {};

TEST_F(BuildWordDocumentXmlTest, PlainParagraph) {
  auto paragraphs = base::JSONReader::Read(
      R"([{"text": "Hello world"}])", base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(paragraphs.has_value());
  std::string xml =
      BuildWordDocumentXml(paragraphs->GetList());

  EXPECT_NE(xml.find("<w:t xml:space=\"preserve\">Hello world</w:t>"),
            std::string::npos);
  // No heading formatting should be present for a plain paragraph.
  EXPECT_EQ(xml.find("<w:rPr>"), std::string::npos);
}

TEST_F(BuildWordDocumentXmlTest, HeadingParagraphIsBoldAndSized) {
  auto paragraphs =
      base::JSONReader::Read(R"([{"text": "Title", "heading_level": 1}])",
                            base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(paragraphs.has_value());
  std::string xml =
      BuildWordDocumentXml(paragraphs->GetList());

  EXPECT_NE(xml.find("<w:b/>"), std::string::npos);
  EXPECT_NE(xml.find("w:sz w:val=\"64\""), std::string::npos);
}

TEST_F(BuildWordDocumentXmlTest, EscapesXmlSpecialCharacters) {
  auto paragraphs =
      base::JSONReader::Read(R"([{"text": "A & B <are> \"friends\""}])",
                            base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(paragraphs.has_value());
  std::string xml =
      BuildWordDocumentXml(paragraphs->GetList());

  EXPECT_NE(xml.find("A &amp; B &lt;are&gt; &quot;friends&quot;"),
            std::string::npos);
  // The raw unescaped characters should not appear inside the run text.
  EXPECT_EQ(xml.find("<are>"), std::string::npos);
}

TEST_F(BuildWordDocumentXmlTest, SkipsEntriesMissingText) {
  auto paragraphs = base::JSONReader::Read(
      R"([{"text": "Kept"}, {"heading_level": 1}, "not an object"])",
      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(paragraphs.has_value());
  std::string xml =
      BuildWordDocumentXml(paragraphs->GetList());

  EXPECT_NE(xml.find("Kept"), std::string::npos);
  // Exactly one <w:p> should have been emitted.
  size_t count = 0;
  size_t pos = 0;
  while ((pos = xml.find("<w:p>", pos)) != std::string::npos) {
    ++count;
    pos += 5;
  }
  EXPECT_EQ(count, 1u);
}

}  // namespace ai_chat

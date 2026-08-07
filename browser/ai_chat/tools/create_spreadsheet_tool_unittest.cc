// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_spreadsheet_tool.h"

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

// Only exercises input-validation failure paths - see the equivalent
// comment in create_word_document_tool_unittest.cc for why a real
// BrowserContext/DownloadManager isn't set up here.
std::string RunToolExpectingValidationError(const std::string& json) {
  CreateSpreadsheetTool tool(/*browser_context=*/nullptr);
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool.UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

}  // namespace

class CreateSpreadsheetToolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CreateSpreadsheetToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunToolExpectingValidationError("not json"),
            "Error: failed to parse input JSON");
}

TEST_F(CreateSpreadsheetToolTest, UseTool_MissingFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(R"({"rows": [["a"]]})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreateSpreadsheetToolTest, UseTool_MissingRows) {
  EXPECT_EQ(RunToolExpectingValidationError(R"({"filename": "budget"})"),
            "Error: missing or empty 'rows' array");
}

TEST_F(CreateSpreadsheetToolTest, UseTool_EmptyRows) {
  EXPECT_EQ(
      RunToolExpectingValidationError(R"({"filename": "budget", "rows": []})"),
      "Error: missing or empty 'rows' array");
}

class ColumnIndexToLettersTest : public testing::Test {};

TEST_F(ColumnIndexToLettersTest, SingleLetterColumns) {
  EXPECT_EQ(internal::ColumnIndexToLetters(0), "A");
  EXPECT_EQ(internal::ColumnIndexToLetters(1), "B");
  EXPECT_EQ(internal::ColumnIndexToLetters(25), "Z");
}

TEST_F(ColumnIndexToLettersTest, DoubleLetterColumns) {
  EXPECT_EQ(internal::ColumnIndexToLetters(26), "AA");
  EXPECT_EQ(internal::ColumnIndexToLetters(27), "AB");
  EXPECT_EQ(internal::ColumnIndexToLetters(51), "AZ");
  EXPECT_EQ(internal::ColumnIndexToLetters(52), "BA");
}

class BuildWorksheetXmlTest : public testing::Test {};

TEST_F(BuildWorksheetXmlTest, TextCellUsesInlineString) {
  auto rows = base::JSONReader::Read(R"([["Name"]])",
                                     base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(rows.has_value());
  std::string xml = internal::BuildWorksheetXml(rows->GetList());

  EXPECT_NE(xml.find(R"(<c r="A1" t="inlineStr"><is><t>Name</t></is></c>)"),
            std::string::npos);
}

TEST_F(BuildWorksheetXmlTest, NumericCellHasNoTypeAttribute) {
  auto rows = base::JSONReader::Read(R"([["42.5"]])",
                                     base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(rows.has_value());
  std::string xml = internal::BuildWorksheetXml(rows->GetList());

  EXPECT_NE(xml.find(R"(<c r="A1"><v>42.5</v></c>)"), std::string::npos);
}

TEST_F(BuildWorksheetXmlTest, MultipleRowsAndColumnsUseCorrectRefs) {
  auto rows = base::JSONReader::Read(R"([["a", "b"], ["c", "d"]])",
                                     base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(rows.has_value());
  std::string xml = internal::BuildWorksheetXml(rows->GetList());

  EXPECT_NE(xml.find(R"(r="A1")"), std::string::npos);
  EXPECT_NE(xml.find(R"(r="B1")"), std::string::npos);
  EXPECT_NE(xml.find(R"(r="A2")"), std::string::npos);
  EXPECT_NE(xml.find(R"(r="B2")"), std::string::npos);
}

TEST_F(BuildWorksheetXmlTest, EscapesXmlSpecialCharacters) {
  auto rows = base::JSONReader::Read(R"([["A & B <are> \"friends\""]])",
                                     base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(rows.has_value());
  std::string xml = internal::BuildWorksheetXml(rows->GetList());

  EXPECT_NE(xml.find("A &amp; B &lt;are&gt; &quot;friends&quot;"),
            std::string::npos);
  EXPECT_EQ(xml.find("<are>"), std::string::npos);
}

}  // namespace ai_chat

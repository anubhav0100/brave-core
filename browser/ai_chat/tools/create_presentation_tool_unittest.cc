// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_presentation_tool.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

// Only exercises input-validation failure paths - see the equivalent
// comment in create_word_document_tool_unittest.cc for why a real
// BrowserContext/DownloadManager isn't set up here.
std::string RunToolExpectingValidationError(const std::string& json) {
  CreatePresentationTool tool(/*browser_context=*/nullptr);
  base::test::TestFuture<Tool::ToolResult, Tool::ToolArtifacts> future;
  tool.UseTool(json, future.GetCallback());
  auto [result, artifacts] = future.Take();
  if (result.empty() || !result[0]->is_text_content_block()) {
    return std::string();
  }
  return result[0]->get_text_content_block()->text;
}

const OoxmlPart* FindPart(const std::vector<OoxmlPart>& parts,
                          const std::string& path) {
  for (const auto& part : parts) {
    if (part.path == path) {
      return &part;
    }
  }
  return nullptr;
}

}  // namespace

class CreatePresentationToolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(CreatePresentationToolTest, UseTool_InvalidJson) {
  EXPECT_EQ(RunToolExpectingValidationError("not json"),
            "Error: failed to parse input JSON");
}

TEST_F(CreatePresentationToolTest, UseTool_MissingFilename) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"slides": [{"title": "Hi"}]})"),
            "Error: missing or empty 'filename'");
}

TEST_F(CreatePresentationToolTest, UseTool_MissingSlides) {
  EXPECT_EQ(RunToolExpectingValidationError(R"({"filename": "pitch"})"),
            "Error: missing or empty 'slides' array");
}

TEST_F(CreatePresentationToolTest, UseTool_EmptySlides) {
  EXPECT_EQ(RunToolExpectingValidationError(
                R"({"filename": "pitch", "slides": []})"),
            "Error: missing or empty 'slides' array");
}

class BuildPresentationPartsTest : public testing::Test {};

TEST_F(BuildPresentationPartsTest, OneSlidePerEntry) {
  auto slides = base::JSONReader::Read(
      R"([{"title": "One", "body": "a"}, {"title": "Two", "body": "b"}])",
      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(slides.has_value());
  std::vector<OoxmlPart> parts =
      internal::BuildPresentationParts(slides->GetList());

  EXPECT_NE(FindPart(parts, "ppt/slides/slide1.xml"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/slides/slide2.xml"), nullptr);
  EXPECT_EQ(FindPart(parts, "ppt/slides/slide3.xml"), nullptr);
}

TEST_F(BuildPresentationPartsTest, IncludesRequiredBoilerplateParts) {
  auto slides = base::JSONReader::Read(
      R"([{"title": "One", "body": "a"}])",
      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(slides.has_value());
  std::vector<OoxmlPart> parts =
      internal::BuildPresentationParts(slides->GetList());

  EXPECT_NE(FindPart(parts, "[Content_Types].xml"), nullptr);
  EXPECT_NE(FindPart(parts, "_rels/.rels"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/presentation.xml"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/_rels/presentation.xml.rels"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/slideMasters/slideMaster1.xml"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/slideLayouts/slideLayout1.xml"), nullptr);
  EXPECT_NE(FindPart(parts, "ppt/theme/theme1.xml"), nullptr);
}

TEST_F(BuildPresentationPartsTest, SlideXmlContainsTitleAndBodyLines) {
  auto slides = base::JSONReader::Read(
      R"([{"title": "My Title", "body": "Line one\nLine two"}])",
      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(slides.has_value());
  std::vector<OoxmlPart> parts =
      internal::BuildPresentationParts(slides->GetList());

  const OoxmlPart* slide = FindPart(parts, "ppt/slides/slide1.xml");
  ASSERT_NE(slide, nullptr);
  EXPECT_NE(slide->content.find("My Title"), std::string::npos);
  EXPECT_NE(slide->content.find("Line one"), std::string::npos);
  EXPECT_NE(slide->content.find("Line two"), std::string::npos);
}

TEST_F(BuildPresentationPartsTest, ContentTypesListsEverySlide) {
  auto slides = base::JSONReader::Read(
      R"([{"title": "One"}, {"title": "Two"}, {"title": "Three"}])",
      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(slides.has_value());
  std::vector<OoxmlPart> parts =
      internal::BuildPresentationParts(slides->GetList());

  const OoxmlPart* content_types = FindPart(parts, "[Content_Types].xml");
  ASSERT_NE(content_types, nullptr);
  EXPECT_NE(content_types->content.find("/ppt/slides/slide1.xml"),
            std::string::npos);
  EXPECT_NE(content_types->content.find("/ppt/slides/slide2.xml"),
            std::string::npos);
  EXPECT_NE(content_types->content.find("/ppt/slides/slide3.xml"),
            std::string::npos);
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_word_document_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameFilename[] = "filename";
constexpr char kPropertyNameParagraphs[] = "paragraphs";
constexpr char kParagraphPropertyText[] = "text";
constexpr char kParagraphPropertyHeadingLevel[] = "heading_level";

}  // namespace

CreateWordDocumentTool::CreateWordDocumentTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreateWordDocumentTool::~CreateWordDocumentTool() = default;

std::string_view CreateWordDocumentTool::Name() const {
  return mojom::kCreateWordDocumentToolName;
}

std::string_view CreateWordDocumentTool::Description() const {
  return "Create a Word (.docx) document from a list of paragraphs and save "
         "it to the user's device via a Save As dialog. Each paragraph is "
         "plain text; optionally mark a paragraph as a heading "
         "(heading_level 1-3) to make it bold and larger. Does not support "
         "images, tables, or rich formatting beyond headings. To edit or "
         "update a document you (or the user, via read_word_document) "
         "already produced, call this tool again with the full desired "
         "paragraph list (previous content plus your changes) and have the "
         "user pick the same file in the Save As dialog to overwrite it.";
}

std::optional<base::DictValue> CreateWordDocumentTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".docx extension is added automatically).")},
       {kPropertyNameParagraphs,
        ArrayProperty(
            "The document's paragraphs, in order.",
            ObjectProperty(
                "A single paragraph",
                {{kParagraphPropertyText, StringProperty("The paragraph's text")},
                 {kParagraphPropertyHeadingLevel,
                  IntegerProperty(
                      "0 for a normal paragraph (default), 1-3 for a "
                      "heading of that level")}}))}});
}

std::optional<std::vector<std::string>>
CreateWordDocumentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename,
                                  kPropertyNameParagraphs};
}

void CreateWordDocumentTool::UseTool(const std::string& input_json,
                                     UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }

  const std::string* filename_value = input->FindString(kPropertyNameFilename);
  if (!filename_value || filename_value->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'filename'"), {});
    return;
  }

  const base::ListValue* paragraphs = input->FindList(kPropertyNameParagraphs);
  if (!paragraphs || paragraphs->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing or empty 'paragraphs' array"),
        {});
    return;
  }

  std::string filename = base::StrCat({*filename_value, ".docx"});

  content::WebContents* web_contents = nullptr;
  if (Profile* profile = Profile::FromBrowserContext(browser_context_)) {
    if (BrowserWindowInterface* browser =
            ProfileBrowserCollection::GetForProfile(profile)
                ->FindTabbedBrowser()) {
      if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
        web_contents = tab->GetContents();
      }
    }
  }
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to show the Save As dialog in."),
        {});
    return;
  }

  std::vector<OoxmlPart> parts;
  parts.push_back({"[Content_Types].xml", kWordContentTypesXml});
  parts.push_back({"_rels/.rels", kWordRootRelsXml});
  parts.push_back(
      {"word/document.xml", BuildWordDocumentXml(*paragraphs)});

  BuildOoxmlArchiveAndSaveAs(
      web_contents, filename, std::move(parts),
      base::BindOnce(&CreateWordDocumentTool::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void CreateWordDocumentTool::OnDownloadComplete(
    UseToolCallback callback,
    std::string filename,
    DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: failed to create '", filename,
                          "': ", result.error_message})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Saved '", filename, "'."})),
      {});
}

}  // namespace ai_chat

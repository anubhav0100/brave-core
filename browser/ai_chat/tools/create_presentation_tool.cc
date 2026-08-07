// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_presentation_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameFilename[] = "filename";
constexpr char kPropertyNameSlides[] = "slides";
constexpr char kSlidePropertyTitle[] = "title";
constexpr char kSlidePropertyBody[] = "body";

constexpr char kContentTypesXmlPrefix[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "content-types\">"
    "<Default Extension=\"rels\" "
    "ContentType=\"application/vnd.openxmlformats-package.relationships+"
    "xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/ppt/presentation.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "presentationml.presentation.main+xml\"/>"
    "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "presentationml.slideMaster+xml\"/>"
    "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "presentationml.slideLayout+xml\"/>"
    "<Override PartName=\"/ppt/theme/theme1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.theme+"
    "xml\"/>";
constexpr char kContentTypesXmlSuffix[] = "</Types>";

constexpr char kRootRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
    "</Relationships>";

constexpr char kSlideMasterXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<p:sldMaster "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/"
    "main\">"
    "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/>"
    "<p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld>"
    "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" "
    "accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" "
    "accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
    "hlink=\"hlink\" folHlink=\"folHlink\"/>"
    "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/>"
    "</p:sldLayoutIdLst></p:sldMaster>";

constexpr char kSlideMasterRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/"
    ">"
    "<Relationship Id=\"rId2\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/theme\" Target=\"../theme/theme1.xml\"/>"
    "</Relationships>";

constexpr char kClrMapOvr[] =
    "<p:clrMapOvr><a:overrideClrMapping bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" "
    "tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" "
    "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
    "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
    "</p:clrMapOvr>";

std::string BuildSlideLayoutXml() {
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<p:sldLayout "
       "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
       "relationships\" "
       "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/"
       "main\" type=\"obj\" preserve=\"1\">"
       "<p:cSld name=\"Title and Content\"><p:spTree>"
       "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/>"
       "</p:nvGrpSpPr><p:grpSpPr/>"
       "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Title Placeholder\"/>"
       "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
       "<p:nvPr><p:ph type=\"title\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
       "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Body Placeholder\"/>"
       "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
       "<p:nvPr><p:ph idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
       "</p:spTree></p:cSld>",
       kClrMapOvr, "</p:sldLayout>"});
}

constexpr char kSlideLayoutRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/slideMaster\" "
    "Target=\"../slideMasters/slideMaster1.xml\"/>"
    "</Relationships>";

constexpr char kSlideRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/"
    ">"
    "</Relationships>";

constexpr char kThemeXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<a:theme "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "name=\"AI Chat Theme\"><a:themeElements>"
    "<a:clrScheme name=\"AI Chat\">"
    "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>"
    "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>"
    "<a:dk2><a:srgbClr val=\"44546A\"/></a:dk2>"
    "<a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
    "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1>"
    "<a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>"
    "<a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3>"
    "<a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>"
    "<a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5>"
    "<a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>"
    "<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>"
    "<a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>"
    "</a:clrScheme>"
    "<a:fontScheme name=\"AI Chat\">"
    "<a:majorFont><a:latin typeface=\"Calibri Light\"/>"
    "<a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
    "<a:minorFont><a:latin typeface=\"Calibri\"/>"
    "<a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
    "</a:fontScheme>"
    "<a:fmtScheme name=\"AI Chat\">"
    "<a:fillStyleLst>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "</a:fillStyleLst>"
    "<a:lnStyleLst>"
    "<a:ln w=\"6350\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "</a:ln>"
    "<a:ln w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/>"
    "</a:solidFill></a:ln>"
    "<a:ln w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/>"
    "</a:solidFill></a:ln>"
    "</a:lnStyleLst>"
    "<a:effectStyleLst>"
    "<a:effectStyle><a:effectLst/></a:effectStyle>"
    "<a:effectStyle><a:effectLst/></a:effectStyle>"
    "<a:effectStyle><a:effectLst/></a:effectStyle>"
    "</a:effectStyleLst>"
    "<a:bgFillStyleLst>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
    "</a:bgFillStyleLst>"
    "</a:fmtScheme>"
    "</a:themeElements></a:theme>";

std::string BuildSlideXml(const std::string& title, const std::string& body) {
  std::string body_paragraphs;
  for (const auto& line :
       base::SplitString(body, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    base::StrAppend(&body_paragraphs,
                    {"<a:p><a:r><a:t>", XmlEscape(line), "</a:t></a:r></a:p>"});
  }

  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<p:sld "
       "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
       "relationships\" "
       "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/"
       "main\">"
       "<p:cSld><p:spTree>"
       "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/>"
       "</p:nvGrpSpPr><p:grpSpPr/>"
       "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Title\"/>"
       "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
       "<p:nvPr><p:ph type=\"title\"/></p:nvPr></p:nvSpPr><p:spPr/>"
       "<p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:t>",
       XmlEscape(title),
       "</a:t></a:r></a:p></p:txBody></p:sp>"
       "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Body\"/>"
       "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
       "<p:nvPr><p:ph idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/>"
       "<p:txBody><a:bodyPr/><a:lstStyle/>",
       body_paragraphs,
       "</p:txBody></p:sp>"
       "</p:spTree></p:cSld>",
       kClrMapOvr, "</p:sld>"});
}

}  // namespace

namespace internal {

std::vector<OoxmlPart> BuildPresentationParts(const base::ListValue& slides) {
  std::string content_types_overrides;
  std::string slide_id_list;
  std::string presentation_rels;
  std::vector<OoxmlPart> parts;

  int slide_number = 0;
  for (const auto& slide_value : slides) {
    ++slide_number;
    const base::DictValue* slide_dict = slide_value.GetIfDict();
    std::string title;
    std::string body;
    if (slide_dict) {
      if (const std::string* title_ptr =
              slide_dict->FindString(kSlidePropertyTitle)) {
        title = *title_ptr;
      }
      if (const std::string* body_ptr =
              slide_dict->FindString(kSlidePropertyBody)) {
        body = *body_ptr;
      }
    }

    std::string slide_num_str = base::NumberToString(slide_number);
    std::string slide_path = base::StrCat({"ppt/slides/slide", slide_num_str, ".xml"});
    parts.push_back({slide_path, BuildSlideXml(title, body)});
    parts.push_back(
        {base::StrCat({"ppt/slides/_rels/slide", slide_num_str, ".xml.rels"}),
         kSlideRelsXml});

    base::StrAppend(&content_types_overrides,
                    {"<Override PartName=\"/", slide_path,
                     "\" "
                     "ContentType=\"application/vnd.openxmlformats-"
                     "officedocument.presentationml.slide+xml\"/>"});

    // Slide ids must be unique and >= 256; relationship ids just need to be
    // unique within presentation.xml.rels.
    base::StrAppend(
        &slide_id_list,
        {"<p:sldId id=\"", base::NumberToString(255 + slide_number),
         "\" r:id=\"rIdSlide", slide_num_str, "\"/>"});
    base::StrAppend(
        &presentation_rels,
        {"<Relationship Id=\"rIdSlide", slide_num_str,
         "\" "
         "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
         "relationships/slide\" Target=\"slides/slide",
         slide_num_str, ".xml\"/>"});
  }

  parts.push_back(
      {"[Content_Types].xml", base::StrCat({kContentTypesXmlPrefix,
                                            content_types_overrides,
                                            kContentTypesXmlSuffix})});
  parts.push_back({"_rels/.rels", kRootRelsXml});
  parts.push_back(
      {"ppt/presentation.xml",
       base::StrCat(
           {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<p:presentation "
            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/"
            "main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/"
            "2006/relationships\" "
            "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/"
            "2006/main\">"
            "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" "
            "r:id=\"rIdMaster1\"/></p:sldMasterIdLst>"
            "<p:sldIdLst>",
            slide_id_list,
            "</p:sldIdLst>"
            "<p:sldSz cx=\"9144000\" cy=\"6858000\"/>"
            "<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
            "</p:presentation>"})});
  parts.push_back(
      {"ppt/_rels/presentation.xml.rels",
       base::StrCat(
           {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships "
            "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
            "relationships\">"
            "<Relationship Id=\"rIdMaster1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
            "relationships/slideMaster\" "
            "Target=\"slideMasters/slideMaster1.xml\"/>",
            presentation_rels, "</Relationships>"})});
  parts.push_back({"ppt/slideMasters/slideMaster1.xml", kSlideMasterXml});
  parts.push_back({"ppt/slideMasters/_rels/slideMaster1.xml.rels",
                   kSlideMasterRelsXml});
  parts.push_back(
      {"ppt/slideLayouts/slideLayout1.xml", BuildSlideLayoutXml()});
  parts.push_back({"ppt/slideLayouts/_rels/slideLayout1.xml.rels",
                   kSlideLayoutRelsXml});
  parts.push_back({"ppt/theme/theme1.xml", kThemeXml});

  return parts;
}

}  // namespace internal

CreatePresentationTool::CreatePresentationTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreatePresentationTool::~CreatePresentationTool() = default;

std::string_view CreatePresentationTool::Name() const {
  return mojom::kCreatePresentationToolName;
}

std::string_view CreatePresentationTool::Description() const {
  return "Create a PowerPoint (.pptx) presentation from a list of slides "
         "(each with a title and body text) and download it to the "
         "user's device. Each line of a slide's body becomes its own "
         "bullet-style paragraph. Does not support images or rich layouts.";
}

std::optional<base::DictValue> CreatePresentationTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".pptx extension is added automatically).")},
       {kPropertyNameSlides,
        ArrayProperty(
            "The presentation's slides, in order.",
            ObjectProperty(
                "A single slide",
                {{kSlidePropertyTitle, StringProperty("The slide's title")},
                 {kSlidePropertyBody,
                  StringProperty("The slide's body text; separate lines "
                                 "with \\n to create multiple bullet-style "
                                 "paragraphs")}}))}});
}

std::optional<std::vector<std::string>>
CreatePresentationTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename, kPropertyNameSlides};
}

void CreatePresentationTool::UseTool(const std::string& input_json,
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

  const base::ListValue* slides = input->FindList(kPropertyNameSlides);
  if (!slides || slides->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'slides' array"),
        {});
    return;
  }

  std::string filename = base::StrCat({*filename_value, ".pptx"});

  BuildOoxmlArchiveAndDownload(
      browser_context_, filename, internal::BuildPresentationParts(*slides),
      base::BindOnce(&CreatePresentationTool::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void CreatePresentationTool::OnDownloadComplete(
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
          base::StrCat({"Created and started downloading '", filename, "'."})),
      {});
}

}  // namespace ai_chat

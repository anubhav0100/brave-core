// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/page_capture_session.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/re2/src/re2/re2.h"

namespace ai_chat {

CapturedPage::CapturedPage() = default;
CapturedPage::CapturedPage(CapturedPage&&) = default;
CapturedPage& CapturedPage::operator=(CapturedPage&&) = default;
CapturedPage::~CapturedPage() = default;

namespace {

void ReplaceAll(std::string* text,
                const std::string& from,
                const std::string& to) {
  base::ReplaceSubstringsAfterOffset(text, 0, from, to);
}

// Strips HTML tags and collapses whitespace, with minimal entity decoding -
// enough for readable plain text without pulling in a full HTML parser.
std::string StripTagsAndCollapseWhitespace(std::string html_fragment) {
  static const base::NoDestructor<re2::RE2> kTagRe(R"(<[^>]*>)");
  RE2::GlobalReplace(&html_fragment, *kTagRe, " ");

  ReplaceAll(&html_fragment, "&nbsp;", " ");
  ReplaceAll(&html_fragment, "&amp;", "&");
  ReplaceAll(&html_fragment, "&lt;", "<");
  ReplaceAll(&html_fragment, "&gt;", ">");
  ReplaceAll(&html_fragment, "&quot;", "\"");
  ReplaceAll(&html_fragment, "&#39;", "'");
  ReplaceAll(&html_fragment, "&apos;", "'");

  static const base::NoDestructor<re2::RE2> kWhitespaceRe(R"(\s+)");
  RE2::GlobalReplace(&html_fragment, *kWhitespaceRe, " ");

  std::string trimmed;
  base::TrimWhitespaceASCII(html_fragment, base::TRIM_ALL, &trimmed);
  return trimmed;
}

struct Section {
  std::string heading;  // Empty for content appearing before any heading.
  std::string text;
};

struct HeadingMatch {
  size_t tag_start = 0;
  size_t tag_end = 0;
  std::string text;
};

std::vector<HeadingMatch> FindHeadings(const std::string& html) {
  std::vector<HeadingMatch> headings;
  static const base::NoDestructor<re2::RE2> kHeadingRe(
      R"((?s)(<h[1-3][^>]*>(.*?)</h[1-3]>))");
  re2::StringPiece input(html);
  std::string full_match;
  std::string heading_text;
  while (RE2::FindAndConsume(&input, *kHeadingRe, &full_match,
                             &heading_text)) {
    size_t tag_end = static_cast<size_t>(input.data() - html.data());
    size_t tag_start = tag_end - full_match.size();
    headings.push_back({tag_start, tag_end, std::move(heading_text)});
  }
  return headings;
}

// Splits `html` into sections at each <h1>-<h3> boundary. Content before the
// first heading becomes one section with an empty heading.
std::vector<Section> SplitIntoSections(const std::string& html) {
  std::vector<Section> sections;
  std::vector<HeadingMatch> headings = FindHeadings(html);

  if (headings.empty()) {
    std::string text = StripTagsAndCollapseWhitespace(html);
    if (!text.empty()) {
      sections.push_back({"", std::move(text)});
    }
    return sections;
  }

  if (headings[0].tag_start > 0) {
    std::string leading =
        StripTagsAndCollapseWhitespace(html.substr(0, headings[0].tag_start));
    if (!leading.empty()) {
      sections.push_back({"", std::move(leading)});
    }
  }

  for (size_t i = 0; i < headings.size(); ++i) {
    size_t body_begin = headings[i].tag_end;
    size_t body_end =
        (i + 1 < headings.size()) ? headings[i + 1].tag_start : html.size();
    sections.push_back(
        {StripTagsAndCollapseWhitespace(headings[i].text),
         StripTagsAndCollapseWhitespace(
             html.substr(body_begin, body_end - body_begin))});
  }
  return sections;
}

// Extracts <a href="...">text</a> links, resolved against `base_url`,
// deduplicated by URL, skipping empty/anchor/javascript hrefs.
std::vector<std::pair<GURL, std::string>> ExtractLinks(const std::string& html,
                                                        const GURL& base_url) {
  std::vector<std::pair<GURL, std::string>> links;
  static const base::NoDestructor<re2::RE2> kLinkRe(
      R"((?s)<a\b[^>]*?href\s*=\s*["']([^"']+)["'][^>]*>(.*?)</a>)");
  re2::StringPiece input(html);
  std::string href;
  std::string inner_html;
  std::set<GURL> seen;
  while (RE2::FindAndConsume(&input, *kLinkRe, &href, &inner_html)) {
    if (href.empty() || href[0] == '#' ||
        base::StartsWith(href, "javascript:",
                         base::CompareCase::INSENSITIVE_ASCII)) {
      continue;
    }
    GURL resolved = base_url.Resolve(href);
    if (!resolved.is_valid() || !seen.insert(resolved).second) {
      continue;
    }
    links.emplace_back(resolved, StripTagsAndCollapseWhitespace(inner_html));
  }
  return links;
}

void AppendParagraph(base::ListValue& paragraphs,
                     const std::string& text,
                     int heading_level) {
  if (text.empty()) {
    return;
  }
  base::DictValue paragraph;
  paragraph.Set("text", text);
  paragraph.Set("heading_level", heading_level);
  paragraphs.Append(std::move(paragraph));
}

content::WebContents* GetActiveWebContentsFor(
    content::BrowserContext* browser_context) {
  Profile* profile = Profile::FromBrowserContext(browser_context);
  if (!profile) {
    return nullptr;
  }
  BrowserWindowInterface* browser =
      ProfileBrowserCollection::GetForProfile(profile)->FindTabbedBrowser();
  if (!browser) {
    return nullptr;
  }
  tabs::TabInterface* tab = browser->GetActiveTabInterface();
  return tab ? tab->GetContents() : nullptr;
}

}  // namespace

PageCaptureSession::PageCaptureSession(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

PageCaptureSession::~PageCaptureSession() = default;

void PageCaptureSession::CaptureActiveTab(ResultCallback callback) {
  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(false, "No active tab to capture.");
    return;
  }

  std::string title = base::UTF16ToUTF8(web_contents->GetTitle());
  GURL page_url = web_contents->GetVisibleURL();
  std::string heading =
      title.empty() ? page_url.spec()
                    : base::StrCat({title, " (", page_url.spec(), ")"});

  page_capture::FetchFullPageSourceRecursive(
      web_contents,
      base::BindOnce(&PageCaptureSession::OnFullPageSourceFetched,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(heading)));
}

void PageCaptureSession::OnFullPageSourceFetched(
    ResultCallback callback,
    std::string heading,
    page_capture::FullPageSource source) {
  if (source.combined_html.empty()) {
    std::move(callback).Run(
        false, "Error: could not read any content from the active tab.");
    return;
  }

  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  GURL page_url =
      web_contents ? web_contents->GetVisibleURL() : GURL(heading);

  CapturedPage page;
  page.heading = heading;
  for (const auto& section : SplitIntoSections(source.combined_html)) {
    if (section.text.empty()) {
      continue;
    }
    AppendParagraph(page.content_paragraphs, section.heading,
                    section.heading.empty() ? 0 : 2);
    AppendParagraph(page.content_paragraphs, section.text, 0);
  }

  std::vector<std::pair<GURL, std::string>> links =
      ExtractLinks(source.combined_html, page_url);
  if (!links.empty()) {
    AppendParagraph(page.content_paragraphs, "Links found on this page", 2);
    std::string links_text;
    for (const auto& [url, text] : links) {
      base::StrAppend(&links_text,
                       {"- ", text.empty() ? url.spec() : text, " (",
                        url.spec(), ")\n"});
    }
    AppendParagraph(page.content_paragraphs, links_text, 0);
  }

  for (const auto& [url, alt] : source.images) {
    page.image_urls.push_back(url);
  }

  size_t page_number = pages_.size() + 1;
  pages_.push_back(std::move(page));
  std::move(callback).Run(
      true, base::StrCat({"Captured page ", base::NumberToString(page_number),
                          " of this session."}));
}

void PageCaptureSession::SaveAsWordDocument(const std::string& filename,
                                            ResultCallback callback) {
  if (pages_.empty()) {
    std::move(callback).Run(false, "Nothing captured yet this session.");
    return;
  }

  std::vector<GURL> all_image_urls;
  for (const auto& page : pages_) {
    all_image_urls.insert(all_image_urls.end(), page.image_urls.begin(),
                          page.image_urls.end());
  }

  auto url_loader_factory =
      browser_context_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();
  FetchImagesForEmbedding(
      url_loader_factory, std::move(all_image_urls),
      base::BindOnce(&PageCaptureSession::OnImagesFetchedForSave,
                     weak_ptr_factory_.GetWeakPtr(), filename,
                     std::move(callback)));
}

void PageCaptureSession::OnImagesFetchedForSave(
    std::string filename,
    ResultCallback callback,
    std::vector<EmbeddedImage> images) {
  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        false, "No open browser window to show the Save As dialog in.");
    return;
  }

  base::ListValue paragraphs;
  for (const auto& page : pages_) {
    AppendParagraph(paragraphs, page.heading, 1);
    for (const auto& paragraph : page.content_paragraphs) {
      paragraphs.Append(paragraph.Clone());
    }
  }

  constexpr int kMaxWidthEmu = 5486400;    // 6 inches.
  constexpr int kDefaultWidthEmu = 3657600;  // 4 inches.
  constexpr int kDefaultHeightEmu = 2743200;  // 3 inches.
  constexpr int kEmuPerPixel = 9525;  // 96 DPI.

  if (!images.empty()) {
    AppendParagraph(paragraphs, "Images", 1);
  }
  std::string document_filename = base::StrCat({filename, ".docx"});
  std::vector<OoxmlPart> parts;
  parts.push_back(
      {"[Content_Types].xml", BuildContentTypesXmlWithImages(images)});
  parts.push_back({"_rels/.rels", kWordRootRelsXml});
  if (!images.empty()) {
    parts.push_back(
        {"word/_rels/document.xml.rels", BuildDocumentRelsXml(images)});
  }
  for (const auto& image : images) {
    int width_emu = kDefaultWidthEmu;
    int height_emu = kDefaultHeightEmu;
    if (image.width_px > 0 && image.height_px > 0) {
      width_emu = image.width_px * kEmuPerPixel;
      height_emu = image.height_px * kEmuPerPixel;
      if (width_emu > kMaxWidthEmu) {
        height_emu = static_cast<int>(static_cast<int64_t>(height_emu) *
                                      kMaxWidthEmu / width_emu);
        width_emu = kMaxWidthEmu;
      }
    }
    base::DictValue image_paragraph;
    image_paragraph.Set("image_relationship_id", image.relationship_id);
    image_paragraph.Set("image_width_emu", width_emu);
    image_paragraph.Set("image_height_emu", height_emu);
    paragraphs.Append(std::move(image_paragraph));

    parts.push_back(
        {base::StrCat({"word/", image.media_path}),
         std::string(image.bytes.begin(), image.bytes.end())});
  }
  parts.push_back({"word/document.xml", BuildWordDocumentXml(paragraphs)});

  BuildOoxmlArchiveAndSaveAs(
      web_contents, document_filename, std::move(parts),
      base::BindOnce(&PageCaptureSession::OnSaveComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     document_filename));
}

void PageCaptureSession::OnSaveComplete(ResultCallback callback,
                                        std::string filename,
                                        DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        false, base::StrCat({"Error: failed to save '", filename, "': ",
                             result.error_message}));
    return;
  }
  std::move(callback).Run(
      true, base::StrCat({"Saved '", filename, "' with ",
                          base::NumberToString(pages_.size()),
                          " captured page(s)."}));
}

void PageCaptureSession::Clear() {
  pages_.clear();
}

}  // namespace ai_chat

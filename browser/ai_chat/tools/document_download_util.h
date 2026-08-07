// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_DOCUMENT_DOWNLOAD_UTIL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_DOCUMENT_DOWNLOAD_UTIL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/image_embed_util.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace ai_chat {

// Escapes text for safe inclusion inside XML element/attribute content.
// Shared by every OOXML-generating tool (Word/Excel/PowerPoint) - these
// generate minimal archives by hand rather than via an XML library, so
// each caller is responsible for escaping its own text content with this
// before embedding it in a part's XML.
std::string XmlEscape(const std::string& text);

// One raw text part to be written into a generated OOXML archive - `path`
// is the path within the archive (e.g. "word/document.xml"), `content` is
// its UTF-8 text.
struct OoxmlPart {
  std::string path;
  std::string content;
};

struct DocumentDownloadResult {
  bool success = false;
  // Only set when `success` is false.
  std::string error_message;
};

using DocumentDownloadCallback =
    base::OnceCallback<void(DocumentDownloadResult)>;

// Builds a minimal OOXML zip archive (the format underlying .docx/.xlsx/
// .pptx) from `parts`, then triggers a native browser download of the
// result named `filename` via `browser_context`'s DownloadManager. All file
// and zip I/O runs on a background sequence; `callback` fires back on the
// calling sequence once the download has been started - this mirrors the
// fire-and-forget shape of the existing "Save Image As"/
// chrome.downloads.download() download call sites (see
// chrome/browser/renderer_context_menu/render_view_context_menu.cc and
// chrome/browser/extensions/api/downloads/downloads_api.cc), it does not
// wait for the download to fully complete.
void BuildOoxmlArchiveAndDownload(content::BrowserContext* browser_context,
                                   const std::string& filename,
                                   std::vector<OoxmlPart> parts,
                                   DocumentDownloadCallback callback);

// Like BuildOoxmlArchiveAndDownload, but presents a native "Save As" dialog
// anchored to `web_contents` and writes the file directly to wherever the
// user chooses, instead of routing through the browser's network-download
// pipeline (DownloadManager::DownloadUrl()). Use this for content that has
// no network origin at all - e.g. a summary captured from a side panel -
// since that download path can depend on a download-protection verdict
// that a purely local, non-network file was never going to get one for.
// `callback` reports success/failure once the user has either picked a
// location and the file was written there, or canceled the dialog.
void BuildOoxmlArchiveAndSaveAs(content::WebContents* web_contents,
                                const std::string& default_filename,
                                std::vector<OoxmlPart> parts,
                                DocumentDownloadCallback callback);

// Presents a native "Open" file dialog anchored to `web_contents`, restricted
// to .docx files, then reads back the plain text of whichever file the user
// picks - extracted from its word/document.xml part, one line per paragraph.
// Always goes through the interactive dialog (never accepts a path directly)
// so the assistant can't read an arbitrary file the user didn't explicitly
// choose. `callback` receives nullopt if the dialog was canceled or the file
// couldn't be read/parsed as a minimal OOXML Word document.
void ShowOpenDialogAndReadWordDocumentText(
    content::WebContents* web_contents,
    base::OnceCallback<void(std::optional<std::string>)> callback);

// Writes `file_bytes` to a temp file and downloads it named `filename`, the
// same way as above but for bytes that are already fully generated (used by
// the PDF tool, whose bytes come from the print-to-pdf pipeline rather than
// a zip archive).
void DownloadGeneratedBytes(content::BrowserContext* browser_context,
                             const std::string& filename,
                             std::vector<uint8_t> file_bytes,
                             DocumentDownloadCallback callback);

// The two static OOXML parts every minimal .docx needs beyond
// word/document.xml itself. Shared so any caller building a Word document
// (the CreateWordDocumentTool agent tool, or any other feature that
// accumulates paragraphs and builds a .docx) doesn't duplicate this
// boilerplate.
extern const char kWordContentTypesXml[];
extern const char kWordRootRelsXml[];

// Builds the word/document.xml part from a flat list of paragraphs. Each is
// a dict, either:
// - a text paragraph: {"text": string, "heading_level": int} (heading_level
//   0 = normal paragraph, 1-3 = a heading of that level rendered
//   bold/larger via direct formatting), or
// - an embedded image: {"image_relationship_id": string (an id from an
//   EmbeddedImage returned by FetchImagesForEmbedding, e.g. "rId2"),
//   "image_width_emu": int, "image_height_emu": int} - the document must
//   also include a matching word/_rels/document.xml.rels part (see
//   BuildDocumentRelsXml) and [Content_Types].xml with that image's
//   extension registered (see BuildContentTypesXmlWithImages), or Word will
//   consider the file corrupt.
// Exposed here (rather than kept private to one tool) so any feature that
// accumulates paragraphs over time - not just a single JSON tool call - can
// build the same document.xml from its own accumulated list at save time.
std::string BuildWordDocumentXml(const base::ListValue& paragraphs);

// [Content_Types].xml and word/_rels/document.xml.rels variants that
// declare `images` (from FetchImagesForEmbedding) so they can be referenced
// by "image_relationship_id" paragraphs in BuildWordDocumentXml. Use these
// instead of kWordContentTypesXml/kWordRootRelsXml (which don't need or
// support a rels/media part) whenever the document embeds at least one
// image; word/_rels/document.xml.rels must be added to the archive's parts
// as its own OoxmlPart alongside each image's own media bytes at
// `image.media_path` (prefixed with "word/").
std::string BuildContentTypesXmlWithImages(
    const std::vector<EmbeddedImage>& images);
std::string BuildDocumentRelsXml(const std::vector<EmbeddedImage>& images);

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_DOCUMENT_DOWNLOAD_UTIL_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_PAGE_CAPTURE_SESSION_H_
#define BRAVE_BROWSER_AI_CHAT_PAGE_CAPTURE_SESSION_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/full_page_source_fetcher.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/browser/ai_chat/tools/image_embed_util.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// One page captured into a PageCaptureSession - its content already split
// into paragraphs/sections/links ready for BuildWordDocumentXml. Images are
// represented as {"pending_image_url": <url>} placeholder paragraphs at the
// exact position they appeared in the page's content (not collected
// separately), so SaveAsWordDocument can swap each one for a real embedded
// picture in place once it's been downloaded - fetched and embedded only at
// save time, since most captures are never saved and there's no point
// downloading images for a session that gets cleared.
struct CapturedPage {
  CapturedPage();
  CapturedPage(CapturedPage&&);
  CapturedPage& operator=(CapturedPage&&);
  ~CapturedPage();

  std::string heading;
  base::ListValue content_paragraphs;
};

// Accumulates pages captured via chat across one conversation, so the user
// can ask the assistant to capture several pages and only get one Save As
// dialog at the end (instead of one per page) - owned by BrowserToolProvider
// so it's shared by CapturePageToSessionTool, SaveCapturedSessionAsWord
// DocumentTool, and ClearCapturedSessionTool, and lives for the conversation.
class PageCaptureSession {
 public:
  using ResultCallback =
      base::OnceCallback<void(bool success, std::string message)>;

  explicit PageCaptureSession(content::BrowserContext* browser_context);
  ~PageCaptureSession();

  PageCaptureSession(const PageCaptureSession&) = delete;
  PageCaptureSession& operator=(const PageCaptureSession&) = delete;

  // Fetches the current active tab's page source, splits it into sections
  // and links, and appends the result to the session.
  void CaptureActiveTab(ResultCallback callback);

  // Builds one Word document from every captured page so far (each as its
  // own titled section), downloads and embeds their images, and shows a
  // native Save As dialog. Does not clear the session - captures can keep
  // being added and saved again (e.g. to a different file, or overwriting
  // the same one to update it).
  void SaveAsWordDocument(const std::string& filename, ResultCallback callback);

  void Clear();

  size_t page_count() const { return pages_.size(); }

 private:
  void OnFullPageSourceFetched(ResultCallback callback,
                               std::string heading,
                               page_capture::FullPageSource source);
  void OnImagesFetchedForSave(std::string filename,
                              ResultCallback callback,
                              std::vector<EmbeddedImage> images);
  void OnSaveComplete(ResultCallback callback,
                      std::string filename,
                      DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::vector<CapturedPage> pages_;

  base::WeakPtrFactory<PageCaptureSession> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_PAGE_CAPTURE_SESSION_H_

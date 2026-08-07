// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_FULL_PAGE_SOURCE_FETCHER_H_
#define BRAVE_BROWSER_AI_CHAT_FULL_PAGE_SOURCE_FETCHER_H_

#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback_forward.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace page_capture {

struct FullPageSource {
  FullPageSource();
  FullPageSource(FullPageSource&&);
  FullPageSource& operator=(FullPageSource&&);
  ~FullPageSource();

  // The main frame's HTML followed by every descendant frame's HTML
  // (iframes, recursively, including nested ones), each preceded by a
  // comment noting which frame it came from.
  std::string combined_html;

  // Every <img> found across all frames, resolved to an absolute URL and
  // paired with its alt text (empty if it had none) - deduplicated by URL,
  // in first-seen order.
  std::vector<std::pair<GURL, std::string>> images;
};

using FullPageSourceCallback = base::OnceCallback<void(FullPageSource)>;

// Reads document.documentElement.outerHTML from `web_contents`'s main frame
// and every descendant frame (iframes, including nested ones), via the same
// isolated-world script injection Speedreader's own page distillation uses
// (see brave/browser/speedreader/page_distiller.cc) - this reads the actual
// page source, including elements that are hidden or off-screen, unlike the
// rendered-text extraction ai_chat::PageContentFetcher normally provides.
//
// Lives in browser/ai_chat (rather than the "Page Capture" side panel that
// originally introduced it) so it can be shared by AI Chat tools too -
// browser/ai_chat cannot depend on brave/browser/ui's "ui" target without
// creating a dependency cycle, since that target already depends on
// browser/ai_chat (for ModelService and the OOXML document builders).
void FetchFullPageSourceRecursive(content::WebContents* web_contents,
                                   FullPageSourceCallback callback);

}  // namespace page_capture

#endif  // BRAVE_BROWSER_AI_CHAT_FULL_PAGE_SOURCE_FETCHER_H_

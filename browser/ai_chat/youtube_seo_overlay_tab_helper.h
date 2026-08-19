// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_YOUTUBE_SEO_OVERLAY_TAB_HELPER_H_
#define BRAVE_BROWSER_AI_CHAT_YOUTUBE_SEO_OVERLAY_TAB_HELPER_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
class RenderFrameHost;
}  // namespace content

class GURL;

namespace ai_chat {

// Shows a small floating "SEO Check" panel (views, tag count, a heuristic
// SEO score, a best-practices checklist, and the video's tags) on any
// youtube.com/watch page, read straight from the page's own embedded
// `window.ytInitialPlayerResponse` - no network request of its own, so it
// works with zero setup. For exact like/comment counts, caption status,
// and channel stats (which aren't in that embedded data), the panel points
// the user at the AI Assistant's analyze_youtube_video_seo tool instead,
// which can use a YouTube Data API key for those - see youtube_seo_tool.h.
//
// Deliberately main-world script execution (like YouTubeScriptInjectorTabHelper
// on Android), not the isolated-world script_injector mojom
// CreatorDetectionScriptInjector uses - reading window.ytInitialPlayerResponse
// requires the main world (an isolated world has its own separate globals
// and would not see it), and there's no security-sensitive data at stake
// here worth isolating from the page the way Rewards' payout-linked creator
// detection is.
class YouTubeSeoOverlayTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<YouTubeSeoOverlayTabHelper> {
 public:
  ~YouTubeSeoOverlayTabHelper() override;

  YouTubeSeoOverlayTabHelper(const YouTubeSeoOverlayTabHelper&) = delete;
  YouTubeSeoOverlayTabHelper& operator=(const YouTubeSeoOverlayTabHelper&) =
      delete;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  WEB_CONTENTS_USER_DATA_KEY_DECL();

 private:
  friend class content::WebContentsUserData<YouTubeSeoOverlayTabHelper>;
  explicit YouTubeSeoOverlayTabHelper(content::WebContents* contents);

  bool IsYouTubeWatchPage() const;
  void MaybeInjectOverlay();

  // The video id last injected for, so repeated calls for the same video
  // (a full DidFinishLoad plus its matching DidFinishNavigation, or a
  // renderer re-announcing the same document) don't inject a second
  // overlay on top of the first.
  std::string last_injected_video_id_;

  // YouTube's own client-side router updates window.ytInitialPlayerResponse
  // asynchronously after a same-document "up next" navigation (a pushState,
  // not a real document load - DidFinishLoad never fires for it, only
  // DidFinishNavigation with IsSameDocument()). This delay gives that
  // update a moment to land before the overlay script reads it; on a real
  // full navigation this is redundant with the DidFinishLoad-triggered
  // injection above (deduped via last_injected_video_id_), so it costs
  // nothing there.
  base::WeakPtrFactory<YouTubeSeoOverlayTabHelper> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_YOUTUBE_SEO_OVERLAY_TAB_HELPER_H_

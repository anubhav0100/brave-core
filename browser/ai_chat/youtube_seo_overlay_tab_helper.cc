// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/youtube_seo_overlay_tab_helper.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "brave/components/ai_chat/core/common/yt_util.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

// Renders a small "SEO Check" panel from window.ytInitialPlayerResponse -
// the same embedded JSON YouTube's own page uses, present once the page has
// loaded. All untrusted text (the video's own title/description/tags, set
// by whoever uploaded it - not the viewer) is HTML-escaped before going
// into the panel's markup, since it's interpolated directly.
constexpr char16_t kOverlayScript[] = uR"(
(function() {
  try {
    // Replace, don't skip: this script only runs once per distinct video
    // (deduped browser-side), but the previous video's panel element is
    // still sitting in the DOM across a same-document "up next"
    // navigation (which never reloads the page), so it must be removed
    // here or it would be stuck showing stale data forever.
    const existing = document.getElementById('__brave_yt_seo_overlay__');
    if (existing) { existing.remove(); }
    const pr = window.ytInitialPlayerResponse;
    if (!pr || !pr.videoDetails) { return; }
    const vd = pr.videoDetails;

    function esc(s) {
      return String(s == null ? '' : s).replace(/[&<>"']/g, function(c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;',
                "'": '&#39;' }[c];
      });
    }

    const tags = Array.isArray(vd.keywords) ? vd.keywords : [];
    const desc = vd.shortDescription || '';
    const title = vd.title || '';
    const views = vd.viewCount ? Number(vd.viewCount) : null;
    const thumbs = (vd.thumbnail && vd.thumbnail.thumbnails) || [];
    const hasMaxRes = thumbs.some(function(t) { return t.width >= 1280; });

    // Heuristic score - mirrors the browser-side analyze_youtube_video_seo
    // tool's weighting for the factors visible from this page alone
    // (tags/description/title length, thumbnail resolution). Like/comment
    // counts and caption status need a YouTube Data API key and aren't in
    // this page's own embedded data, so they get a fixed neutral share
    // here rather than being guessed at.
    let score = 0;
    score += tags.length === 0 ? 0 : Math.min(25, 5 + tags.length * 2);
    score += Math.min(25, Math.floor(desc.length / 20));
    const titleLen = title.length;
    score += (titleLen >= 40 && titleLen <= 70) ? 15
            : (titleLen >= 20 && titleLen <= 100) ? 8
            : (titleLen > 0) ? 3 : 0;
    score += hasMaxRes ? 10 : 3;
    score += 12;

    const host = document.createElement('div');
    host.id = '__brave_yt_seo_overlay__';
    host.style.cssText =
      'position:fixed;top:70px;left:12px;z-index:2147483000;';
    const shadow = host.attachShadow({ mode: 'open' });
    shadow.innerHTML =
      '<style>' +
      '.panel{width:260px;font-family:Roboto,Arial,sans-serif;' +
        'background:#fff;color:#0f0f0f;border-radius:10px;' +
        'box-shadow:0 2px 12px rgba(0,0,0,.25);overflow:hidden;' +
        'border:1px solid #e5e5e5}' +
      '.head{display:flex;align-items:center;justify-content:space-between;' +
        'background:#0f0f0f;color:#fff;padding:8px 10px;font-size:13px;' +
        'font-weight:600}' +
      '.head button{background:transparent;border:none;color:#fff;' +
        'cursor:pointer;font-size:15px;line-height:1;padding:0 2px}' +
      '.section{padding:8px 10px;border-top:1px solid #eee}' +
      '.section h4{margin:0 0 6px;font-size:11px;text-transform:uppercase;' +
        'color:#606060;letter-spacing:.03em}' +
      '.row{display:flex;justify-content:space-between;font-size:12px;' +
        'margin-bottom:3px}' +
      '.score{font-size:20px;font-weight:700;color:#1a73e8}' +
      '.tags{font-size:11px;color:#606060;word-break:break-word}' +
      '.hint{font-size:10.5px;color:#909090;padding:6px 10px 8px}' +
      'ul{margin:0;padding-left:16px;font-size:12px}' +
      'li.no{color:#c00}li.yes{color:#188038}' +
      '</style>' +
      '<div class="panel">' +
        '<div class="head"><span>YouTube SEO Check</span>' +
          '<button id="close" aria-label="Close">×</button></div>' +
        '<div class="section"><h4>Summary</h4>' +
          '<div class="row"><span>Views</span><span>' +
            (views !== null ? views.toLocaleString() : '—') +
            '</span></div></div>' +
        '<div class="section"><h4>SEO</h4>' +
          '<div class="score">' + score + '/100</div>' +
          '<div class="row"><span>Tags found</span><span>' + tags.length +
            '</span></div>' +
          '<div class="row"><span>Description length</span><span>' +
            desc.length + ' chars</span></div></div>' +
        '<div class="section"><h4>Best practices</h4><ul>' +
          '<li class="' + (tags.length ? 'yes' : 'no') +
            '">Tags added</li>' +
          '<li class="' + (desc.length >= 200 ? 'yes' : 'no') +
            '">Description 200+ chars</li>' +
          '<li class="' + (hasMaxRes ? 'yes' : 'no') +
            '">High-res thumbnail</li></ul></div>' +
        '<div class="section"><h4>Tags</h4><div class="tags">' +
          (tags.length ? esc(tags.slice(0, 20).join(', ')) : '(none found)') +
          '</div></div>' +
        '<div class="hint">Ask the AI Assistant to analyze this video\'s ' +
          'SEO for exact like/comment counts and channel stats.</div>' +
      '</div>';
    const closeButton = shadow.querySelector('#close');
    if (closeButton) {
      closeButton.addEventListener('click', function() { host.remove(); });
    }
    document.documentElement.appendChild(host);
  } catch (e) {}
})();
)";

}  // namespace

YouTubeSeoOverlayTabHelper::YouTubeSeoOverlayTabHelper(
    content::WebContents* contents)
    : WebContentsObserver(contents),
      content::WebContentsUserData<YouTubeSeoOverlayTabHelper>(*contents) {}

YouTubeSeoOverlayTabHelper::~YouTubeSeoOverlayTabHelper() = default;

void YouTubeSeoOverlayTabHelper::PrimaryPageChanged(content::Page& page) {
  last_injected_video_id_.clear();
}

void YouTubeSeoOverlayTabHelper::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  if (!render_frame_host->IsInPrimaryMainFrame()) {
    return;
  }
  MaybeInjectOverlay();
}

void YouTubeSeoOverlayTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Only same-document navigations need handling here - a real (different
  // document) navigation is already covered by DidFinishLoad above, and
  // window.ytInitialPlayerResponse wouldn't exist yet at this point in a
  // fresh document's lifecycle anyway. This is what catches YouTube's own
  // "up next" in-app navigation between videos, which never triggers a
  // full document load.
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      !navigation_handle->IsSameDocument()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&YouTubeSeoOverlayTabHelper::MaybeInjectOverlay,
                     weak_ptr_factory_.GetWeakPtr()),
      base::Milliseconds(500));
}

bool YouTubeSeoOverlayTabHelper::IsYouTubeWatchPage() const {
  const GURL& url = web_contents()->GetLastCommittedURL();
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (!kYouTubeHosts.contains(url.host())) {
    return false;
  }
  return url.path() == "/watch";
}

void YouTubeSeoOverlayTabHelper::MaybeInjectOverlay() {
  if (!IsYouTubeWatchPage()) {
    return;
  }
  const GURL& url = web_contents()->GetLastCommittedURL();
  std::string video_id;
  if (!net::GetValueForKeyInQuery(url, "v", &video_id) || video_id.empty()) {
    return;
  }
  if (video_id == last_injected_video_id_) {
    return;
  }

  content::RenderFrameHost* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }
  content::RenderFrameHost::AllowInjectingJavaScript();
  rfh->ExecuteJavaScript(kOverlayScript, base::NullCallback());
  last_injected_video_id_ = video_id;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(YouTubeSeoOverlayTabHelper);

}  // namespace ai_chat

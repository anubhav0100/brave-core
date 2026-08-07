// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/full_page_source_fetcher.h"

#include <set>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/re2/src/re2/re2.h"

namespace page_capture {

FullPageSource::FullPageSource() = default;
FullPageSource::FullPageSource(FullPageSource&&) = default;
FullPageSource& FullPageSource::operator=(FullPageSource&&) = default;
FullPageSource::~FullPageSource() = default;

namespace {

constexpr char16_t kGetOuterHtml[] =
    u"document.documentElement ? document.documentElement.outerHTML : ''";

std::vector<std::pair<GURL, std::string>> ExtractImages(
    const std::string& html,
    const GURL& base_url) {
  std::vector<std::pair<GURL, std::string>> images;
  static const base::NoDestructor<re2::RE2> kImgTagRe(R"((<img\b[^>]*>))");
  static const base::NoDestructor<re2::RE2> kSrcRe(
      R"(\bsrc\s*=\s*["']([^"']+)["'])");
  static const base::NoDestructor<re2::RE2> kAltRe(
      R"(\balt\s*=\s*["']([^"']*)["'])");

  re2::StringPiece input(html);
  std::string tag;
  while (RE2::FindAndConsume(&input, *kImgTagRe, &tag)) {
    std::string src;
    if (!RE2::PartialMatch(tag, *kSrcRe, &src)) {
      continue;
    }
    GURL resolved = base_url.Resolve(src);
    if (!resolved.is_valid()) {
      continue;
    }
    std::string alt;
    RE2::PartialMatch(tag, *kAltRe, &alt);
    images.emplace_back(std::move(resolved), std::move(alt));
  }
  return images;
}

// Owns one in-flight recursive fetch across every frame of a WebContents.
// Self-deleting - lives only for the duration of the fetch.
class Fetcher {
 public:
  static void Start(content::WebContents* web_contents,
                     FullPageSourceCallback callback) {
    new Fetcher(web_contents, std::move(callback));
  }

 private:
  Fetcher(content::WebContents* web_contents, FullPageSourceCallback callback)
      : callback_(std::move(callback)) {
    std::vector<content::RenderFrameHost*> frames;
    web_contents->ForEachRenderFrameHost(
        [&frames](content::RenderFrameHost* rfh) {
          if (rfh->IsRenderFrameLive()) {
            frames.push_back(rfh);
          }
        });

    if (frames.empty()) {
      std::move(callback_).Run({});
      delete this;
      return;
    }

    frame_urls_.resize(frames.size());
    html_by_frame_.resize(frames.size());
    base::RepeatingClosure barrier = base::BarrierClosure(
        frames.size(), base::BindOnce(&Fetcher::OnAllFramesDone,
                                       weak_factory_.GetWeakPtr()));
    for (size_t i = 0; i < frames.size(); ++i) {
      frame_urls_[i] = frames[i]->GetLastCommittedURL();
      frames[i]->ExecuteJavaScriptInIsolatedWorld(
          kGetOuterHtml,
          base::BindOnce(&Fetcher::OnFrameResult, weak_factory_.GetWeakPtr(),
                         i, barrier),
          ISOLATED_WORLD_ID_BRAVE_INTERNAL);
    }

    // A single unresponsive frame (a stuck cross-origin iframe, a detached
    // one, etc.) would otherwise wait on the barrier forever and hang the
    // whole capture. Force-complete with whatever frames answered in time.
    timeout_timer_.Start(FROM_HERE, base::Seconds(8),
                         base::BindOnce(&Fetcher::OnAllFramesDone,
                                        weak_factory_.GetWeakPtr()));
  }

  void OnFrameResult(size_t index,
                      base::RepeatingClosure barrier,
                      base::Value result) {
    if (result.is_string()) {
      html_by_frame_[index] = result.GetString();
    }
    barrier.Run();
  }

  void OnAllFramesDone() {
    if (done_) {
      return;
    }
    done_ = true;
    timeout_timer_.Stop();

    FullPageSource source;
    std::set<GURL> seen_images;
    for (size_t i = 0; i < html_by_frame_.size(); ++i) {
      if (html_by_frame_[i].empty()) {
        continue;
      }
      base::StrAppend(&source.combined_html,
                       {"<!-- Frame: ", frame_urls_[i].spec(), " -->\n",
                        html_by_frame_[i], "\n\n"});
      for (auto& image : ExtractImages(html_by_frame_[i], frame_urls_[i])) {
        if (seen_images.insert(image.first).second) {
          source.images.push_back(std::move(image));
        }
      }
    }
    std::move(callback_).Run(std::move(source));
    delete this;
  }

  FullPageSourceCallback callback_;
  std::vector<GURL> frame_urls_;
  std::vector<std::string> html_by_frame_;
  bool done_ = false;
  base::OneShotTimer timeout_timer_;
  base::WeakPtrFactory<Fetcher> weak_factory_{this};
};

}  // namespace

void FetchFullPageSourceRecursive(content::WebContents* web_contents,
                                   FullPageSourceCallback callback) {
  Fetcher::Start(web_contents, std::move(callback));
}

}  // namespace page_capture

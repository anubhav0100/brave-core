// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_YOUTUBE_SEO_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_YOUTUBE_SEO_TOOL_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

class GURL;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace api_request_helper {
class APIRequestHelper;
class APIRequestResult;
}  // namespace api_request_helper

namespace ai_chat {

// Stores the user's own YouTube Data API v3 key (a free Google Cloud key -
// console.cloud.google.com, enable "YouTube Data API v3"), so
// AnalyzeYouTubeVideoSeoTool can fetch exact stats instead of only what's
// visible on the currently-open page. Mirrors SetN8nApiKeyTool's shape.
class SetYouTubeApiKeyTool : public Tool {
 public:
  explicit SetYouTubeApiKeyTool(content::BrowserContext* browser_context);
  ~SetYouTubeApiKeyTool() override;

  SetYouTubeApiKeyTool(const SetYouTubeApiKeyTool&) = delete;
  SetYouTubeApiKeyTool& operator=(const SetYouTubeApiKeyTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Fetches a YouTube video's stats, tags, and description, and reports a
// heuristic SEO score plus a best-practices checklist the AI Assistant can
// use to give the user concrete ranking suggestions - the same kind of
// summary a third-party "YouTube SEO" extension overlay shows, but read on
// demand rather than injected into the page.
//
// Two data sources, in priority order:
// 1. The YouTube Data API v3, if the user has set a key (see
//    SetYouTubeApiKeyTool) - exact view/like/comment counts, the full tag
//    list, caption availability, and channel subscriber/video/total-view
//    counts, for ANY video by URL or id.
// 2. Otherwise, a one-shot read of `window.ytInitialPlayerResponse` on the
//    active tab, if it's already showing the requested video - title, an
//    approximate view count, tags, and description, but no like/comment
//    counts, caption status, or channel stats (those aren't in the public
//    page's own embedded data).
//
// Some checklist items real YouTube Studio-based tools show (comment
// pinned, chapters/cards/end screen added, thumbnail A/B test status) need
// the creator's own Studio session and aren't obtainable from either
// source here - the report says so explicitly rather than guessing.
class AnalyzeYouTubeVideoSeoTool : public Tool {
 public:
  explicit AnalyzeYouTubeVideoSeoTool(content::BrowserContext* browser_context);
  ~AnalyzeYouTubeVideoSeoTool() override;

  AnalyzeYouTubeVideoSeoTool(const AnalyzeYouTubeVideoSeoTool&) = delete;
  AnalyzeYouTubeVideoSeoTool& operator=(const AnalyzeYouTubeVideoSeoTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void FetchViaDataApi(const std::string& video_id,
                       const std::string& api_key,
                       UseToolCallback callback);
  void OnVideoResponse(const std::string& api_key,
                       UseToolCallback callback,
                       api_request_helper::APIRequestResult result);
  void OnChannelResponse(base::DictValue video_item,
                         UseToolCallback callback,
                         api_request_helper::APIRequestResult result);
  void BuildAndReturnReportFromApi(
      base::DictValue video_item,
      std::optional<base::DictValue> channel_statistics,
      UseToolCallback callback);

  void FetchViaPageScrape(content::WebContents* web_contents,
                          const std::string& video_id,
                          UseToolCallback callback);
  void OnPageScrapeResult(const std::string& video_id,
                          UseToolCallback callback,
                          base::Value result);

  content::WebContents* GetActiveWebContentsIfShowing(
      const std::string& video_id);

  void EnsureApiRequestHelper();

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;
  base::WeakPtrFactory<AnalyzeYouTubeVideoSeoTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_YOUTUBE_SEO_TOOL_H_

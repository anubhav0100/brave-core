// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/youtube_seo_tool.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "brave/browser/ai_chat/tools/tab_utils.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/pref_names.h"
#include "brave/components/ai_chat/core/common/yt_util.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameApiKey[] = "api_key";
constexpr char kPropertyNameVideoUrl[] = "video_url";

constexpr char kYouTubeDataApiBaseUrl[] =
    "https://www.googleapis.com/youtube/v3/";

// Reads window.ytInitialPlayerResponse, the same embedded JSON blob
// YouTube's own page renders from - present on any loaded /watch page,
// main-world only (an isolated world would not see it). Returns null if
// it's missing (page not fully loaded, or not a video page after all).
constexpr char16_t kExtractYouTubeDataScript[] = uR"(
(function() {
  try {
    const pr = window.ytInitialPlayerResponse;
    if (!pr || !pr.videoDetails) { return null; }
    const vd = pr.videoDetails;
    const thumbs = (vd.thumbnail && vd.thumbnail.thumbnails) || [];
    const hasMaxRes = thumbs.some(function(t) { return t.width >= 1280; });
    return {
      title: vd.title || '',
      videoId: vd.videoId || '',
      channelId: vd.channelId || '',
      author: vd.author || '',
      viewCount: vd.viewCount || '',
      lengthSeconds: vd.lengthSeconds || '',
      isLiveContent: !!vd.isLiveContent,
      shortDescription: vd.shortDescription || '',
      keywords: Array.isArray(vd.keywords) ? vd.keywords : [],
      hasMaxResThumbnail: hasMaxRes
    };
  } catch (e) {
    return null;
  }
})()
)";

// Parses a YouTube URL (youtube.com/watch?v=, youtu.be/, m.youtube.com) or
// a bare 11-character video id.
std::optional<std::string> ExtractYouTubeVideoId(const std::string& input) {
  std::string trimmed;
  base::TrimWhitespaceASCII(input, base::TRIM_ALL, &trimmed);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  GURL url(trimmed);
  if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
    if (kYouTubeHosts.contains(url.host()) || url.host() == "youtube.com") {
      std::string value;
      if (net::GetValueForKeyInQuery(url, "v", &value) && !value.empty()) {
        return value;
      }
      return std::nullopt;
    }
    if (url.host() == "youtu.be") {
      std::string_view path = url.path();
      if (path.size() > 1) {
        return std::string(path.substr(1));
      }
    }
    return std::nullopt;
  }

  // Not a URL - accept it as a bare id if it looks like one (YouTube video
  // ids are 11 characters of [A-Za-z0-9_-]).
  if (trimmed.size() == 11 &&
      std::ranges::all_of(trimmed, [](char c) {
        return base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '_' ||
               c == '-';
      })) {
    return trimmed;
  }
  return std::nullopt;
}

std::optional<int64_t> GetInt64FromApiString(const base::DictValue* dict,
                                             std::string_view key) {
  if (!dict) {
    return std::nullopt;
  }
  const std::string* value = dict->FindString(key);
  if (!value) {
    return std::nullopt;
  }
  int64_t parsed = 0;
  if (!base::StringToInt64(*value, &parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::string FormatCount(std::optional<int64_t> count) {
  return count.has_value() ? base::NumberToString(*count) : "unknown";
}

std::string FormatKnown(std::optional<bool> value) {
  if (!value.has_value()) {
    return "unknown (set a YouTube Data API key for this)";
  }
  return *value ? "yes" : "no";
}

// Inputs for the shared report/score builder - some fields are only ever
// populated from the Data API path (like/comment counts, captions, channel
// stats), left as nullopt when the report comes from a page scrape.
struct SeoReportInputs {
  std::string source;
  std::string title;
  std::string description;
  std::vector<std::string> tags;
  std::optional<int64_t> view_count;
  std::optional<int64_t> like_count;
  std::optional<int64_t> comment_count;
  std::optional<bool> has_captions;
  std::optional<bool> has_maxres_thumbnail;
  std::string channel_title;
  std::optional<int64_t> subscriber_count;
  std::optional<int64_t> channel_total_views;
  std::optional<int64_t> channel_video_count;
};

// A heuristic 0-100 estimate, not a reproduction of any third-party tool's
// proprietary scoring - weighted toward the factors most directly under the
// uploader's control (tags, description, title, thumbnail, captions), plus
// a small engagement-ratio signal when view/like counts are both known.
// Unknown boolean/ratio factors get half credit rather than zero, so a
// page-scrape-only report (no API key set) isn't unfairly penalized for
// data it simply couldn't see.
int ComputeSeoScore(const SeoReportInputs& in) {
  int score = 0;
  score += in.tags.empty() ? 0 : std::min<int>(25, 5 + static_cast<int>(in.tags.size()) * 2);
  score += std::min<int>(25, static_cast<int>(in.description.size()) / 20);
  size_t title_len = in.title.size();
  if (title_len >= 40 && title_len <= 70) {
    score += 15;
  } else if (title_len >= 20 && title_len <= 100) {
    score += 8;
  } else if (title_len > 0) {
    score += 3;
  }
  score += in.has_captions.has_value() ? (*in.has_captions ? 15 : 0) : 7;
  score += in.has_maxres_thumbnail.has_value()
              ? (*in.has_maxres_thumbnail ? 10 : 3)
              : 5;
  if (in.view_count.has_value() && in.like_count.has_value() &&
      *in.view_count > 0) {
    double ratio = static_cast<double>(*in.like_count) / *in.view_count;
    score += ratio >= 0.04 ? 10 : ratio >= 0.02 ? 6 : ratio > 0 ? 3 : 0;
  } else {
    score += 5;
  }
  return std::min(100, score);
}

std::string BuildSeoReport(const SeoReportInputs& in) {
  int score = ComputeSeoScore(in);
  std::vector<std::string> lines;
  lines.push_back(base::StrCat({"# YouTube SEO analysis: ", in.title}));
  lines.push_back(base::StrCat({"(data source: ", in.source, ")"}));
  lines.push_back("");
  lines.push_back("## Summary");
  lines.push_back(base::StrCat({"- Views: ", FormatCount(in.view_count)}));
  lines.push_back(base::StrCat({"- Likes: ", FormatCount(in.like_count)}));
  lines.push_back(
      base::StrCat({"- Comments: ", FormatCount(in.comment_count)}));
  lines.push_back("");
  lines.push_back(base::StrCat(
      {"## SEO score (heuristic): ", base::NumberToString(score), "/100"}));
  lines.push_back(base::StrCat({"- Tags found: ",
                                base::NumberToString(in.tags.size())}));
  lines.push_back(base::StrCat(
      {"- Description length: ",
       base::NumberToString(in.description.size()), " characters"}));
  lines.push_back("");
  if (!in.channel_title.empty() || in.subscriber_count || in.channel_total_views ||
      in.channel_video_count) {
    lines.push_back("## Channel");
    if (!in.channel_title.empty()) {
      lines.push_back(base::StrCat({"- Channel: ", in.channel_title}));
    }
    lines.push_back(
        base::StrCat({"- Subscribers: ", FormatCount(in.subscriber_count)}));
    lines.push_back(base::StrCat(
        {"- Total channel views: ", FormatCount(in.channel_total_views)}));
    lines.push_back(
        base::StrCat({"- Total videos: ", FormatCount(in.channel_video_count)}));
    lines.push_back("");
  }
  lines.push_back("## Best practices");
  lines.push_back(base::StrCat(
      {"- Tags added: ", in.tags.empty() ? "no" : "yes"}));
  lines.push_back(base::StrCat(
      {"- Description is 200+ characters: ",
       in.description.size() >= 200 ? "yes" : "no"}));
  lines.push_back(
      base::StrCat({"- Captions available: ", FormatKnown(in.has_captions)}));
  lines.push_back(base::StrCat(
      {"- High-res (1280px+) thumbnail: ",
       FormatKnown(in.has_maxres_thumbnail)}));
  lines.push_back(
      "- Comment pinned / chapters / cards / end screen added: not "
      "checkable from here - only visible in YouTube Studio's own editor "
      "for this video, which this tool doesn't have access to.");
  lines.push_back("");
  lines.push_back("## Tags");
  lines.push_back(in.tags.empty() ? "(none found)"
                                  : base::JoinString(in.tags, ", "));
  lines.push_back("");
  lines.push_back("## Description");
  lines.push_back(in.description.empty() ? "(none)" : in.description);
  lines.push_back("");
  lines.push_back(
      "Use the above to suggest concrete ranking improvements: better "
      "tags/keywords for the title and description, whether the "
      "description needs to be longer or more keyword-rich, and whether "
      "the thumbnail/captions gaps above are worth fixing.");
  return base::JoinString(lines, "\n");
}

}  // namespace

// SetYouTubeApiKeyTool -------------------------------------------------------

SetYouTubeApiKeyTool::SetYouTubeApiKeyTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

SetYouTubeApiKeyTool::~SetYouTubeApiKeyTool() = default;

std::string_view SetYouTubeApiKeyTool::Name() const {
  return "set_youtube_api_key";
}

std::string_view SetYouTubeApiKeyTool::Description() const {
  return "Stores the user's YouTube Data API v3 key (a free key from "
         "console.cloud.google.com with 'YouTube Data API v3' enabled), so "
         "analyze_youtube_video_seo can fetch exact stats for any video "
         "instead of only what's visible on the currently-open page.";
}

std::optional<base::DictValue> SetYouTubeApiKeyTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameApiKey, StringProperty("The YouTube Data API v3 key.")}});
}

std::optional<std::vector<std::string>>
SetYouTubeApiKeyTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameApiKey};
}

void SetYouTubeApiKeyTool::UseTool(const std::string& input_json,
                                   UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* api_key =
      input.has_value() ? input->FindString(kPropertyNameApiKey) : nullptr;
  if (!api_key || api_key->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'api_key'"), {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  if (!prefs) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no profile to store this in."),
        {});
    return;
  }
  prefs->SetString(prefs::kBraveAIChatYouTubeDataApiKey, *api_key);
  std::move(callback).Run(
      CreateContentBlocksForText("Saved the YouTube Data API key."), {});
}

// AnalyzeYouTubeVideoSeoTool --------------------------------------------------

AnalyzeYouTubeVideoSeoTool::AnalyzeYouTubeVideoSeoTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

AnalyzeYouTubeVideoSeoTool::~AnalyzeYouTubeVideoSeoTool() = default;

std::string_view AnalyzeYouTubeVideoSeoTool::Name() const {
  return "analyze_youtube_video_seo";
}

std::string_view AnalyzeYouTubeVideoSeoTool::Description() const {
  return "Analyzes a YouTube video's SEO: view/like/comment counts, tags, "
         "description, a heuristic SEO score, a best-practices checklist, "
         "and channel stats (subscribers/total views/video count) when a "
         "YouTube Data API key is set (see set_youtube_api_key). Without a "
         "key, falls back to reading whatever's visible on the video's own "
         "page - which only works if that exact video is open in the "
         "current tab, and gives less data (no like/comment counts, no "
         "caption status, no channel stats). Use this to give the user "
         "concrete suggestions for ranking their video higher.";
}

std::optional<base::DictValue> AnalyzeYouTubeVideoSeoTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameVideoUrl,
        StringProperty(
            "A YouTube video URL (youtube.com/watch?v=..., youtu.be/...) or "
            "bare video id to analyze. If omitted, analyzes the video open "
            "in the current tab.")}});
}

std::optional<std::vector<std::string>>
AnalyzeYouTubeVideoSeoTool::RequiredProperties() const {
  return std::nullopt;
}

void AnalyzeYouTubeVideoSeoTool::UseTool(const std::string& input_json,
                                         UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* video_url =
      input.has_value() ? input->FindString(kPropertyNameVideoUrl) : nullptr;

  std::optional<std::string> video_id;
  if (video_url && !video_url->empty()) {
    video_id = ExtractYouTubeVideoId(*video_url);
    if (!video_id) {
      std::move(callback).Run(
          CreateContentBlocksForText(base::StrCat(
              {"Error: couldn't find a YouTube video id in '", *video_url,
               "'."})),
          {});
      return;
    }
  } else if (content::WebContents* active =
                 GetActiveWebContentsFor(browser_context_)) {
    video_id = ExtractYouTubeVideoId(active->GetLastCommittedURL().spec());
  }

  if (!video_id) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no YouTube video specified, and the current tab isn't "
            "a YouTube video page. Either open a YouTube video, or pass a "
            "video_url."),
        {});
    return;
  }

  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key =
      prefs ? prefs->GetString(prefs::kBraveAIChatYouTubeDataApiKey)
            : std::string();
  if (!api_key.empty()) {
    FetchViaDataApi(*video_id, api_key, std::move(callback));
    return;
  }

  content::WebContents* matching_tab =
      GetActiveWebContentsIfShowing(*video_id);
  if (matching_tab) {
    FetchViaPageScrape(matching_tab, *video_id, std::move(callback));
    return;
  }

  std::move(callback).Run(
      CreateContentBlocksForText(
          "Error: no YouTube Data API key is set (use set_youtube_api_key) "
          "and the requested video isn't open in the current tab. Either "
          "open that video's page, or set an API key to analyze any video "
          "by id/URL."),
      {});
}

content::WebContents* AnalyzeYouTubeVideoSeoTool::GetActiveWebContentsIfShowing(
    const std::string& video_id) {
  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    return nullptr;
  }
  auto current_id =
      ExtractYouTubeVideoId(web_contents->GetLastCommittedURL().spec());
  return (current_id && *current_id == video_id) ? web_contents : nullptr;
}

void AnalyzeYouTubeVideoSeoTool::EnsureApiRequestHelper() {
  if (api_request_helper_) {
    return;
  }
  static const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("ai_chat_youtube_seo_tool", R"(
        semantics {
          sender: "AI Chat YouTube SEO Tool"
          description:
            "Fetches a YouTube video's public stats, tags, and channel "
            "stats from the YouTube Data API v3, using the user's own API "
            "key, so the AI Assistant can suggest SEO improvements."
          trigger:
            "The AI Assistant uses the analyze_youtube_video_seo tool "
            "while responding to the user's request."
          data: "A YouTube video id and the user's own YouTube Data API "
                "key."
          destination: OTHER
          destination_other: "The YouTube Data API (googleapis.com)."
          internal {
            contacts {
              email: "ai-chat@brave.com"
            }
          }
          user_data {
            type: NONE
          }
          last_reviewed: "2026-08-18"
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled independently of AI Chat; "
            "it only runs if the user has set a YouTube Data API key."
          policy_exception_justification:
            "Not covered by a dedicated policy - only runs against a key "
            "the user explicitly provided."
        })");
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      kTrafficAnnotation, std::move(url_loader_factory));
}

void AnalyzeYouTubeVideoSeoTool::FetchViaDataApi(const std::string& video_id,
                                                 const std::string& api_key,
                                                 UseToolCallback callback) {
  EnsureApiRequestHelper();
  GURL url(base::StrCat(
      {kYouTubeDataApiBaseUrl,
       "videos?part=snippet,statistics,contentDetails&id=", video_id,
       "&key=", base::EscapeQueryParamValue(api_key, true)}));
  api_request_helper_->Request(
      "GET", url, "", "",
      base::BindOnce(&AnalyzeYouTubeVideoSeoTool::OnVideoResponse,
                     weak_ptr_factory_.GetWeakPtr(), api_key,
                     std::move(callback)));
}

void AnalyzeYouTubeVideoSeoTool::OnVideoResponse(
    const std::string& api_key,
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode() || !result.value_body().is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: the YouTube Data API returned HTTP ",
             base::NumberToString(result.response_code()),
             " for the video lookup - check the API key and that the "
             "video id is correct."})),
        {});
    return;
  }
  const base::ListValue* items = result.value_body().GetDict().FindList("items");
  if (!items || items->empty() || !(*items)[0].is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no video found with that id (it may be private, "
            "deleted, or the id is wrong)."),
        {});
    return;
  }
  base::DictValue item = (*items)[0].GetDict().Clone();

  const std::string* channel_id = nullptr;
  if (const base::DictValue* snippet = item.FindDict("snippet")) {
    channel_id = snippet->FindString("channelId");
  }
  if (!channel_id || channel_id->empty()) {
    BuildAndReturnReportFromApi(std::move(item), std::nullopt,
                               std::move(callback));
    return;
  }

  GURL channel_url(base::StrCat(
      {kYouTubeDataApiBaseUrl, "channels?part=statistics&id=", *channel_id,
       "&key=", base::EscapeQueryParamValue(api_key, true)}));
  api_request_helper_->Request(
      "GET", channel_url, "", "",
      base::BindOnce(&AnalyzeYouTubeVideoSeoTool::OnChannelResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(item),
                     std::move(callback)));
}

void AnalyzeYouTubeVideoSeoTool::OnChannelResponse(
    base::DictValue video_item,
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  std::optional<base::DictValue> channel_statistics;
  if (result.Is2XXResponseCode() && result.value_body().is_dict()) {
    const base::ListValue* items =
        result.value_body().GetDict().FindList("items");
    if (items && !items->empty() && (*items)[0].is_dict()) {
      if (const base::DictValue* stats =
              (*items)[0].GetDict().FindDict("statistics")) {
        channel_statistics = stats->Clone();
      }
    }
  }
  BuildAndReturnReportFromApi(std::move(video_item),
                             std::move(channel_statistics),
                             std::move(callback));
}

void AnalyzeYouTubeVideoSeoTool::BuildAndReturnReportFromApi(
    base::DictValue video_item,
    std::optional<base::DictValue> channel_statistics,
    UseToolCallback callback) {
  SeoReportInputs in;
  in.source = "YouTube Data API v3";

  if (const base::DictValue* snippet = video_item.FindDict("snippet")) {
    if (const std::string* title = snippet->FindString("title")) {
      in.title = *title;
    }
    if (const std::string* description = snippet->FindString("description")) {
      in.description = *description;
    }
    if (const std::string* channel_title = snippet->FindString("channelTitle")) {
      in.channel_title = *channel_title;
    }
    if (const base::ListValue* tags = snippet->FindList("tags")) {
      for (const auto& tag : *tags) {
        if (tag.is_string()) {
          in.tags.push_back(tag.GetString());
        }
      }
    }
    if (const base::DictValue* thumbnails = snippet->FindDict("thumbnails")) {
      in.has_maxres_thumbnail = thumbnails->FindDict("maxres") != nullptr;
    }
  }

  const base::DictValue* statistics = video_item.FindDict("statistics");
  in.view_count = GetInt64FromApiString(statistics, "viewCount");
  in.like_count = GetInt64FromApiString(statistics, "likeCount");
  in.comment_count = GetInt64FromApiString(statistics, "commentCount");

  if (const base::DictValue* content_details =
          video_item.FindDict("contentDetails")) {
    if (const std::string* caption = content_details->FindString("caption")) {
      in.has_captions = (*caption == "true");
    }
  }

  if (channel_statistics) {
    in.subscriber_count =
        GetInt64FromApiString(&*channel_statistics, "subscriberCount");
    in.channel_total_views =
        GetInt64FromApiString(&*channel_statistics, "viewCount");
    in.channel_video_count =
        GetInt64FromApiString(&*channel_statistics, "videoCount");
  }

  std::move(callback).Run(CreateContentBlocksForText(BuildSeoReport(in)), {});
}

void AnalyzeYouTubeVideoSeoTool::FetchViaPageScrape(
    content::WebContents* web_contents,
    const std::string& video_id,
    UseToolCallback callback) {
  content::RenderFrameHost* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: couldn't read this tab's page - try reloading it."),
        {});
    return;
  }
  content::RenderFrameHost::AllowInjectingJavaScript();
  rfh->ExecuteJavaScript(
      kExtractYouTubeDataScript,
      base::BindOnce(&AnalyzeYouTubeVideoSeoTool::OnPageScrapeResult,
                     weak_ptr_factory_.GetWeakPtr(), video_id,
                     std::move(callback)));
}

void AnalyzeYouTubeVideoSeoTool::OnPageScrapeResult(
    const std::string& video_id,
    UseToolCallback callback,
    base::Value result) {
  if (!result.is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: couldn't read this video's data from the page - try "
            "reloading it, or set a YouTube Data API key with "
            "set_youtube_api_key for a more reliable path."),
        {});
    return;
  }
  const base::DictValue& data = result.GetDict();

  SeoReportInputs in;
  in.source = "the current page (no YouTube Data API key set)";
  if (const std::string* title = data.FindString("title")) {
    in.title = *title;
  }
  if (const std::string* description = data.FindString("shortDescription")) {
    in.description = *description;
  }
  if (const std::string* author = data.FindString("author")) {
    in.channel_title = *author;
  }
  if (const base::ListValue* keywords = data.FindList("keywords")) {
    for (const auto& keyword : *keywords) {
      if (keyword.is_string()) {
        in.tags.push_back(keyword.GetString());
      }
    }
  }
  if (const std::string* view_count = data.FindString("viewCount")) {
    int64_t parsed = 0;
    if (base::StringToInt64(*view_count, &parsed)) {
      in.view_count = parsed;
    }
  }
  if (std::optional<bool> has_maxres = data.FindBool("hasMaxResThumbnail")) {
    in.has_maxres_thumbnail = has_maxres;
  }

  std::move(callback).Run(CreateContentBlocksForText(BuildSeoReport(in)), {});
}

}  // namespace ai_chat

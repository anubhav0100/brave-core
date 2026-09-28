// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/open_maps_search_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/ai_chat/tools/tab_utils.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "content/public/browser/web_contents.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {
constexpr char kPropertyQuery[] = "query";
constexpr char kPropertyCampaignId[] = "campaign_id";

// Matches Brave_AI_Lead_Assistant_Development_Blueprint.md section 7.1's
// buildMapsSearchUrl exactly - a fixed, documented Maps URL template
// (developers.google.com/maps/documentation/urls/get-started), not an
// arbitrary URL. No API key is required for this URL form.
GURL BuildMapsSearchUrl(const std::string& query) {
  GURL url("https://www.google.com/maps/search/");
  url = net::AppendOrReplaceQueryParameter(url, "api", "1");
  url = net::AppendOrReplaceQueryParameter(url, "query", query);
  return url;
}
}  // namespace

OpenMapsSearchTool::OpenMapsSearchTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

OpenMapsSearchTool::~OpenMapsSearchTool() = default;

std::string_view OpenMapsSearchTool::Name() const {
  return mojom::kOpenMapsSearchToolName;
}

std::string_view OpenMapsSearchTool::Description() const {
  return "Opens a Google Maps search (e.g. \"industrial equipment "
         "manufacturers Faridabad\") in a new tab for lead research. "
         "Follow immediately with extract_maps_listings_tool to read the "
         "actual results (business names, ratings, phone numbers), then "
         "use save_lead to record the ones that fit the campaign before "
         "moving to the next query.";
}

std::optional<base::DictValue> OpenMapsSearchTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyQuery,
       StringProperty("The Maps search query, e.g. \"industrial equipment "
                      "manufacturers Faridabad\"")},
      {kPropertyCampaignId,
       StringProperty("The campaign this search is for (optional)")},
  });
}

std::optional<std::vector<std::string>>
OpenMapsSearchTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyQuery});
}

void OpenMapsSearchTool::UseTool(const std::string& input_json,
                                 UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* query =
      input ? input->FindString(kPropertyQuery) : nullptr;
  if (!query || query->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'query'"), {});
    return;
  }

  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to open a tab in."),
        {});
    return;
  }
  web_contents->OpenURL(
      {BuildMapsSearchUrl(*query), content::Referrer(),
       WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK,
       /*is_renderer_initiated=*/false},
      /*navigation_handle_callback=*/{});

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Opened a Maps search for \"", *query,
           "\" in a new tab. Call extract_maps_listings_tool next to read "
           "the results."})),
      {});
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/extract_maps_listings_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/ai_chat/tools/tab_utils.h"
#include "brave/components/ai_chat/content/browser/page_content_blocks.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/content_extraction/ai_page_content.mojom.h"

namespace ai_chat {

ExtractMapsListingsTool::ExtractMapsListingsTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ExtractMapsListingsTool::~ExtractMapsListingsTool() = default;

std::string_view ExtractMapsListingsTool::Name() const {
  return mojom::kExtractMapsListingsToolName;
}

std::string_view ExtractMapsListingsTool::Description() const {
  return "Reads the currently active tab's visible content - call this "
         "immediately after open_maps_search_tool to see the actual "
         "business names, ratings, addresses and phone numbers in the "
         "results, instead of asking the user to type them in. Work "
         "through one search at a time (open, extract, save_lead the ones "
         "that fit, then move to the next query) rather than opening many "
         "tabs first.";
}

void ExtractMapsListingsTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no open browser tab to read."),
        {});
    return;
  }

  auto options = blink::mojom::AIPageContentOptions::New();
  options->mode = blink::mojom::AIPageContentMode::kActionableElements;

  optimization_guide::GetAIPageContent(
      web_contents, std::move(options),
      base::BindOnce(&ExtractMapsListingsTool::OnPageContent,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ExtractMapsListingsTool::OnPageContent(
    UseToolCallback callback,
    optimization_guide::AIPageContentResultOrError content) {
  if (!content.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: could not read this tab's content."),
        {});
    return;
  }

  const auto& apc = content->proto;
  if (!apc.has_root_node()) {
    std::move(callback).Run(
        CreateContentBlocksForText("This tab has no readable content yet - "
                                  "it may still be loading."),
        {});
    return;
  }

  std::move(callback).Run(
      ConvertAnnotatedPageContentToBlocks(apc, PageContentDetail::kContentOnly),
      {});
}

}  // namespace ai_chat

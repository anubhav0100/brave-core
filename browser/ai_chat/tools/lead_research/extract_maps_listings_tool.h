// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXTRACT_MAPS_LISTINGS_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXTRACT_MAPS_LISTINGS_TOOL_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "components/optimization_guide/content/browser/page_content_proto_provider.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Reads the rendered content (business names, ratings, addresses, phone
// numbers, etc.) of the currently active tab - meant to be called right
// after open_maps_search_tool opens a search, so the model can see the
// results itself instead of asking the user to copy them in. Uses the same
// AnnotatedPageContent extraction "summarize this page" already uses for
// any other site - reading a page the AI itself navigated to on the user's
// behalf is not meaningfully different from that, and is not the
// bulk/automated Maps Platform API scraping Google's API terms restrict
// (no API key is used anywhere in this flow - see open_maps_search_tool's
// own comment). What actually gets kept is still curated: only listings
// the model explicitly chooses via save_lead end up recorded anywhere.
class ExtractMapsListingsTool : public Tool {
 public:
  explicit ExtractMapsListingsTool(content::BrowserContext* browser_context);
  ~ExtractMapsListingsTool() override;

  ExtractMapsListingsTool(const ExtractMapsListingsTool&) = delete;
  ExtractMapsListingsTool& operator=(const ExtractMapsListingsTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

  base::WeakPtr<Tool> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  void OnPageContent(UseToolCallback callback,
                     optimization_guide::AIPageContentResultOrError content);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::WeakPtrFactory<ExtractMapsListingsTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXTRACT_MAPS_LISTINGS_TOOL_H_

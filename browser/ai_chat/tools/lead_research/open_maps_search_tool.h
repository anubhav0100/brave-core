// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_MAPS_SEARCH_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_MAPS_SEARCH_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Opens a Google Maps search in a new tab for the user's own visual lead
// research - see Brave_AI_Lead_Assistant_Development_Blueprint.md section
// 7.1. Builds the URL from a fixed, trusted template
// (google.com/maps/search/?api=1&query=...) - never an arbitrary
// model-supplied URL, and never scrolls, reads, or extracts the Maps
// results itself. Maps Platform terms restrict extracting/storing listing
// content into a database, so this deliberately stops at "open the search
// for the user to look at."
class OpenMapsSearchTool : public Tool {
 public:
  explicit OpenMapsSearchTool(content::BrowserContext* browser_context);
  ~OpenMapsSearchTool() override;

  OpenMapsSearchTool(const OpenMapsSearchTool&) = delete;
  OpenMapsSearchTool& operator=(const OpenMapsSearchTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_OPEN_MAPS_SEARCH_TOOL_H_

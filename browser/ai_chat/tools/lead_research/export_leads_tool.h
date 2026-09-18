// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXPORT_LEADS_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXPORT_LEADS_TOOL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Exports a campaign's saved leads as a downloaded CSV file - see
// Brave_AI_Lead_Assistant_Development_Blueprint.md section 13
// (POST /api/exports) and section 14's requirement to protect exports
// against CSV formula injection from untrusted company names/notes.
class ExportLeadsTool : public Tool {
 public:
  explicit ExportLeadsTool(content::BrowserContext* browser_context);
  ~ExportLeadsTool() override;

  ExportLeadsTool(const ExportLeadsTool&) = delete;
  ExportLeadsTool& operator=(const ExportLeadsTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnDownloadComplete(UseToolCallback callback,
                          std::string filename,
                          DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  base::WeakPtrFactory<ExportLeadsTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEAD_RESEARCH_EXPORT_LEADS_TOOL_H_

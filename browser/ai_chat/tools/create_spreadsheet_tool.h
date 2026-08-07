// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_SPREADSHEET_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_SPREADSHEET_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

namespace internal {

// Converts a 0-based column index to Excel column letters (0 -> "A",
// 25 -> "Z", 26 -> "AA", ...). Exposed for unit tests.
std::string ColumnIndexToLetters(int index);

// Builds the xl/worksheets/sheet1.xml part from a "rows" JSON array (each
// entry an array of cell values, each a string). A cell whose value parses
// as a number is emitted as a numeric cell; otherwise as inline text.
// Exposed for unit tests.
std::string BuildWorksheetXml(const base::ListValue& rows);

}  // namespace internal

// Exposes a tool that lets the assistant create an Excel (.xlsx)
// spreadsheet from a 2D grid of cell values and triggers a native browser
// download of the result. Builds a minimal single-sheet OOXML package
// directly via document_download_util.h. V1 scope is intentionally limited
// to one sheet of plain text/number values; no formulas, multiple sheets,
// or charts.
class CreateSpreadsheetTool : public Tool {
 public:
  explicit CreateSpreadsheetTool(content::BrowserContext* browser_context);
  ~CreateSpreadsheetTool() override;

  CreateSpreadsheetTool(const CreateSpreadsheetTool&) = delete;
  CreateSpreadsheetTool& operator=(const CreateSpreadsheetTool&) = delete;

  // Tool:
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

  base::WeakPtrFactory<CreateSpreadsheetTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_SPREADSHEET_TOOL_H_

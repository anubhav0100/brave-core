// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_SPREADSHEET_MEMORY_SESSION_H_
#define BRAVE_BROWSER_AI_CHAT_SPREADSHEET_MEMORY_SESSION_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// One row the assistant added to a SpreadsheetMemorySession - `sheet_name`
// groups rows into separate worksheets in the saved workbook (sheets appear
// in first-seen order), `cells` are that row's column values left to right.
struct SpreadsheetRow {
  std::string sheet_name;
  std::vector<std::string> cells;
};

// Accumulates spreadsheet rows across one conversation - e.g. leads found
// one at a time while researching Google Maps results - so many rows added
// over several tool calls (and several separate named sheets, like "Leads"
// and "Progress") can be merged into one .xlsx on request instead of
// create_spreadsheet_tool's one-shot single-call/single-sheet/immediate-
// download shape. Owned by BrowserToolProvider so it's shared by
// AddSpreadsheetRowTool, DownloadSpreadsheetTool, and ClearSpreadsheetTool,
// and lives for the conversation - mirrors ResponseMemorySession's shape
// exactly, just for rows instead of labeled text blocks.
class SpreadsheetMemorySession {
 public:
  using ResultCallback =
      base::OnceCallback<void(bool success, std::string message)>;

  explicit SpreadsheetMemorySession(content::BrowserContext* browser_context);
  ~SpreadsheetMemorySession();

  SpreadsheetMemorySession(const SpreadsheetMemorySession&) = delete;
  SpreadsheetMemorySession& operator=(const SpreadsheetMemorySession&) =
      delete;

  // Appends one row to `sheet_name` (creating it, in first-seen order,
  // if it hasn't been used yet this session). Does not save anything to
  // disk yet.
  void AddRow(const std::string& sheet_name, std::vector<std::string> cells);

  // Builds one .xlsx workbook - one worksheet per distinct sheet_name used
  // in AddRow so far, each with every row added under that name, in the
  // order added - and triggers a native browser download. Does not clear
  // the session, so more rows can be added and this called again to get an
  // updated file merging everything accumulated so far. Fails with an
  // error if no rows have been added yet.
  void SaveAsSpreadsheet(const std::string& filename, ResultCallback callback);

  void Clear();

  size_t row_count() const { return rows_.size(); }

 private:
  void OnSaveComplete(ResultCallback callback,
                      std::string filename,
                      DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::vector<SpreadsheetRow> rows_;

  base::WeakPtrFactory<SpreadsheetMemorySession> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_SPREADSHEET_MEMORY_SESSION_H_

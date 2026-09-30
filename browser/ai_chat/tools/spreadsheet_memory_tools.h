// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_SPREADSHEET_MEMORY_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_SPREADSHEET_MEMORY_TOOLS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace ai_chat {

class SpreadsheetMemorySession;

// Adds one row to this conversation's spreadsheet-memory session, under an
// optional named sheet (defaults to one sheet if omitted). Does not save
// anything to disk yet. Call this once per row found over however many
// steps the task takes (e.g. one call per qualifying lead while working
// through search results), then call download_spreadsheet to merge every
// row added so far into one .xlsx.
class AddSpreadsheetRowTool : public Tool {
 public:
  explicit AddSpreadsheetRowTool(SpreadsheetMemorySession* session);
  ~AddSpreadsheetRowTool() override;

  AddSpreadsheetRowTool(const AddSpreadsheetRowTool&) = delete;
  AddSpreadsheetRowTool& operator=(const AddSpreadsheetRowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<SpreadsheetMemorySession> session_ = nullptr;

  base::WeakPtrFactory<AddSpreadsheetRowTool> weak_ptr_factory_{this};
};

// Builds one .xlsx workbook from every row added with add_spreadsheet_row
// so far this conversation - one worksheet per distinct sheet name used,
// each in the order rows were added - and downloads it. Does not clear the
// session, so more rows can be added and this called again to get an
// updated file merging everything added so far - only call this when the
// user actually asks for the file, not after every row. Fails with an
// error if nothing has been added yet.
class DownloadSpreadsheetTool : public Tool {
 public:
  explicit DownloadSpreadsheetTool(SpreadsheetMemorySession* session);
  ~DownloadSpreadsheetTool() override;

  DownloadSpreadsheetTool(const DownloadSpreadsheetTool&) = delete;
  DownloadSpreadsheetTool& operator=(const DownloadSpreadsheetTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnSaveComplete(UseToolCallback callback,
                      bool success,
                      std::string message);

  raw_ptr<SpreadsheetMemorySession> session_ = nullptr;

  base::WeakPtrFactory<DownloadSpreadsheetTool> weak_ptr_factory_{this};
};

// Clears this conversation's spreadsheet-memory session, discarding every
// row added with add_spreadsheet_row so far, so the next one added starts
// a fresh workbook.
class ClearSpreadsheetTool : public Tool {
 public:
  explicit ClearSpreadsheetTool(SpreadsheetMemorySession* session);
  ~ClearSpreadsheetTool() override;

  ClearSpreadsheetTool(const ClearSpreadsheetTool&) = delete;
  ClearSpreadsheetTool& operator=(const ClearSpreadsheetTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<SpreadsheetMemorySession> session_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_SPREADSHEET_MEMORY_TOOLS_H_

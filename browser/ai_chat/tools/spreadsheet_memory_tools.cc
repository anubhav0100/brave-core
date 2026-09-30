// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/spreadsheet_memory_tools.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ai_chat/spreadsheet_memory_session.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyNameSheetName[] = "sheet_name";
constexpr char kPropertyNameCells[] = "cells";
constexpr char kPropertyNameFilename[] = "filename";
}  // namespace

// AddSpreadsheetRowTool ----------------------------------------------------

AddSpreadsheetRowTool::AddSpreadsheetRowTool(SpreadsheetMemorySession* session)
    : session_(session) {}

AddSpreadsheetRowTool::~AddSpreadsheetRowTool() = default;

std::string_view AddSpreadsheetRowTool::Name() const {
  return mojom::kAddSpreadsheetRowToolName;
}

std::string_view AddSpreadsheetRowTool::Description() const {
  return "Adds one row of cell values to this conversation's "
         "spreadsheet-memory session, under an optional named sheet (all "
         "rows go to one default sheet if you never pass sheet_name - pass "
         "it consistently, e.g. \"Leads\" and \"Progress\", if you want "
         "separate sheets in the final workbook). Does not save anything "
         "to disk yet - call this once per row as you find them, then call "
         "download_spreadsheet only once the user actually wants the file.";
}

std::optional<base::DictValue> AddSpreadsheetRowTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameSheetName,
        StringProperty("Which sheet this row belongs to, e.g. \"Leads\". "
                       "Optional - omit to use one default sheet.")},
       {kPropertyNameCells,
        ArrayProperty("This row's cell values, left to right.",
                      StringProperty("A cell's value"))}});
}

std::optional<std::vector<std::string>>
AddSpreadsheetRowTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameCells};
}

void AddSpreadsheetRowTool::UseTool(const std::string& input_json,
                                    UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no spreadsheet memory session available."),
        {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const base::ListValue* cells_list = input->FindList(kPropertyNameCells);
  if (!cells_list || cells_list->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'cells' array"),
        {});
    return;
  }
  std::vector<std::string> cells;
  cells.reserve(cells_list->size());
  for (const auto& cell_value : *cells_list) {
    const std::string* cell_text = cell_value.GetIfString();
    cells.push_back(cell_text ? *cell_text : "");
  }
  const std::string* sheet_name = input->FindString(kPropertyNameSheetName);
  session_->AddRow(sheet_name ? *sheet_name : "", std::move(cells));

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Added row (", base::NumberToString(session_->row_count()),
           " row(s) in the session so far)."})),
      {});
}

// DownloadSpreadsheetTool ---------------------------------------------------

DownloadSpreadsheetTool::DownloadSpreadsheetTool(
    SpreadsheetMemorySession* session)
    : session_(session) {}

DownloadSpreadsheetTool::~DownloadSpreadsheetTool() = default;

std::string_view DownloadSpreadsheetTool::Name() const {
  return mojom::kDownloadSpreadsheetToolName;
}

std::string_view DownloadSpreadsheetTool::Description() const {
  return "Builds one .xlsx workbook from every row added with "
         "add_spreadsheet_row so far this conversation - one worksheet per "
         "distinct sheet name used - and downloads it. Does not clear the "
         "session, so more rows can be added and this called again to get "
         "an updated file merging everything added so far. Only call this "
         "when the user actually asks for the file, not after every row.";
}

std::optional<base::DictValue> DownloadSpreadsheetTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".xlsx extension is added automatically).")}});
}

std::optional<std::vector<std::string>>
DownloadSpreadsheetTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename};
}

void DownloadSpreadsheetTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  if (!session_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no spreadsheet memory session available."),
        {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const std::string* filename = input->FindString(kPropertyNameFilename);
  if (!filename || filename->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'filename'"), {});
    return;
  }
  session_->SaveAsSpreadsheet(
      *filename, base::BindOnce(&DownloadSpreadsheetTool::OnSaveComplete,
                                weak_ptr_factory_.GetWeakPtr(),
                                std::move(callback)));
}

void DownloadSpreadsheetTool::OnSaveComplete(UseToolCallback callback,
                                             bool success,
                                             std::string message) {
  std::move(callback).Run(CreateContentBlocksForText(message), {});
}

// ClearSpreadsheetTool -------------------------------------------------------

ClearSpreadsheetTool::ClearSpreadsheetTool(SpreadsheetMemorySession* session)
    : session_(session) {}

ClearSpreadsheetTool::~ClearSpreadsheetTool() = default;

std::string_view ClearSpreadsheetTool::Name() const {
  return mojom::kClearSpreadsheetToolName;
}

std::string_view ClearSpreadsheetTool::Description() const {
  return "Clears this conversation's spreadsheet-memory session, "
         "discarding every row added with add_spreadsheet_row so far, so "
         "the next one added starts a fresh workbook.";
}

void ClearSpreadsheetTool::UseTool(const std::string& input_json,
                                   UseToolCallback callback) {
  if (session_) {
    session_->Clear();
  }
  std::move(callback).Run(
      CreateContentBlocksForText("Cleared the spreadsheet memory session."),
      {});
}

}  // namespace ai_chat

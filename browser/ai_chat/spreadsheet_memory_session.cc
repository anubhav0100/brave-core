// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/spreadsheet_memory_session.h"

#include <map>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace ai_chat {

namespace {

// Same base-26 column-letter algorithm as create_spreadsheet_tool.cc's
// internal::ColumnIndexToLetters - kept as its own small copy here (rather
// than shared) since that one is exposed specifically for that tool's own
// unit tests and this session's multi-sheet output shape differs enough
// (grouped by sheet name, built incrementally) that sharing the surrounding
// XML-template code wouldn't save much.
std::string ColumnIndexToLetters(int index) {
  std::string letters;
  ++index;
  while (index > 0) {
    int remainder = (index - 1) % 26;
    letters.insert(letters.begin(), static_cast<char>('A' + remainder));
    index = (index - 1) / 26;
  }
  return letters;
}

std::string XmlEscapeCell(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      default:
        escaped += c;
    }
  }
  return escaped;
}

std::string BuildWorksheetXmlForRows(
    const std::vector<std::vector<std::string>>& rows) {
  std::string sheet_data;
  int row_number = 0;
  for (const auto& row : rows) {
    ++row_number;
    std::string row_cells;
    int column_index = 0;
    for (const auto& cell_text : row) {
      std::string cell_ref = base::StrCat(
          {ColumnIndexToLetters(column_index), base::NumberToString(row_number)});
      ++column_index;

      double numeric_value = 0;
      if (!cell_text.empty() &&
          base::StringToDouble(cell_text, &numeric_value)) {
        base::StrAppend(&row_cells,
                        {"<c r=\"", cell_ref, "\"><v>", cell_text, "</v></c>"});
      } else {
        base::StrAppend(
            &row_cells,
            {"<c r=\"", cell_ref, "\" t=\"inlineStr\"><is><t>",
             XmlEscapeCell(cell_text), "</t></is></c>"});
      }
    }
    base::StrAppend(&sheet_data, {"<row r=\"", base::NumberToString(row_number),
                                  "\">", row_cells, "</row>"});
  }

  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<worksheet "
       "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/"
       "main\"><sheetData>",
       sheet_data, "</sheetData></worksheet>"});
}

std::string BuildContentTypesXml(size_t sheet_count) {
  std::string overrides;
  for (size_t i = 1; i <= sheet_count; ++i) {
    base::StrAppend(&overrides,
                    {"<Override PartName=\"/xl/worksheets/sheet",
                     base::NumberToString(i),
                     ".xml\" "
                     "ContentType=\"application/vnd.openxmlformats-"
                     "officedocument.spreadsheetml.worksheet+xml\"/>"});
  }
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<Types "
       "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
       "content-types\">"
       "<Default Extension=\"rels\" "
       "ContentType=\"application/vnd.openxmlformats-package.relationships+"
       "xml\"/>"
       "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
       "<Override PartName=\"/xl/workbook.xml\" "
       "ContentType=\"application/vnd.openxmlformats-officedocument."
       "spreadsheetml.sheet.main+xml\"/>",
       overrides, "</Types>"});
}

constexpr char kRootRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
    "</Relationships>";

std::string BuildWorkbookXml(const std::vector<std::string>& sheet_names) {
  std::string sheets;
  for (size_t i = 0; i < sheet_names.size(); ++i) {
    base::StrAppend(&sheets,
                    {"<sheet name=\"", XmlEscapeCell(sheet_names[i]),
                     "\" sheetId=\"", base::NumberToString(i + 1),
                     "\" r:id=\"rId", base::NumberToString(i + 1), "\"/>"});
  }
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<workbook "
       "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
       "relationships\">"
       "<sheets>",
       sheets, "</sheets></workbook>"});
}

std::string BuildWorkbookRelsXml(size_t sheet_count) {
  std::string rels;
  for (size_t i = 1; i <= sheet_count; ++i) {
    base::StrAppend(&rels,
                    {"<Relationship Id=\"rId", base::NumberToString(i),
                     "\" "
                     "Type=\"http://schemas.openxmlformats.org/"
                     "officeDocument/2006/relationships/worksheet\" "
                     "Target=\"worksheets/sheet",
                     base::NumberToString(i), ".xml\"/>"});
  }
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<Relationships "
       "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
       "relationships\">",
       rels, "</Relationships>"});
}

}  // namespace

SpreadsheetMemorySession::SpreadsheetMemorySession(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

SpreadsheetMemorySession::~SpreadsheetMemorySession() = default;

void SpreadsheetMemorySession::AddRow(const std::string& sheet_name,
                                      std::vector<std::string> cells) {
  rows_.push_back({sheet_name.empty() ? "Sheet1" : sheet_name,
                   std::move(cells)});
}

void SpreadsheetMemorySession::SaveAsSpreadsheet(const std::string& filename,
                                                  ResultCallback callback) {
  if (rows_.empty()) {
    std::move(callback).Run(
        false,
        "Error: no rows have been added yet - call add_spreadsheet_row "
        "first.");
    return;
  }

  // Group rows by sheet name, preserving first-seen sheet order.
  std::vector<std::string> sheet_names;
  std::map<std::string, std::vector<std::vector<std::string>>> sheet_rows;
  for (const auto& row : rows_) {
    if (!sheet_rows.contains(row.sheet_name)) {
      sheet_names.push_back(row.sheet_name);
    }
    sheet_rows[row.sheet_name].push_back(row.cells);
  }

  std::vector<OoxmlPart> parts;
  parts.push_back({"[Content_Types].xml", BuildContentTypesXml(sheet_names.size())});
  parts.push_back({"_rels/.rels", kRootRelsXml});
  parts.push_back({"xl/workbook.xml", BuildWorkbookXml(sheet_names)});
  parts.push_back(
      {"xl/_rels/workbook.xml.rels", BuildWorkbookRelsXml(sheet_names.size())});
  for (size_t i = 0; i < sheet_names.size(); ++i) {
    parts.push_back({base::StrCat({"xl/worksheets/sheet",
                                   base::NumberToString(i + 1), ".xml"}),
                     BuildWorksheetXmlForRows(sheet_rows[sheet_names[i]])});
  }

  std::string full_filename = base::StrCat({filename, ".xlsx"});
  BuildOoxmlArchiveAndDownload(
      browser_context_, full_filename, std::move(parts),
      base::BindOnce(&SpreadsheetMemorySession::OnSaveComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     full_filename));
}

void SpreadsheetMemorySession::OnSaveComplete(ResultCallback callback,
                                              std::string filename,
                                              DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        false, base::StrCat({"Error: failed to save '", filename, "': ",
                             result.error_message}));
    return;
  }
  std::move(callback).Run(
      true, base::StrCat({"Saved and started downloading '", filename,
                          "' with ", base::NumberToString(rows_.size()),
                          " row(s) across the sheets added so far."}));
}

void SpreadsheetMemorySession::Clear() {
  rows_.clear();
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_spreadsheet_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameFilename[] = "filename";
constexpr char kPropertyNameRows[] = "rows";

}  // namespace

namespace internal {

std::string ColumnIndexToLetters(int index) {
  std::string letters;
  ++index;  // Work in 1-based terms so the algorithm below is simple base-26.
  while (index > 0) {
    int remainder = (index - 1) % 26;
    letters.insert(letters.begin(), static_cast<char>('A' + remainder));
    index = (index - 1) / 26;
  }
  return letters;
}

std::string BuildWorksheetXml(const base::ListValue& rows) {
  std::string sheet_data;
  int row_number = 0;
  for (const auto& row_value : rows) {
    ++row_number;
    const base::ListValue* row = row_value.GetIfList();
    if (!row) {
      continue;
    }

    std::string row_cells;
    int column_index = 0;
    for (const auto& cell_value : *row) {
      std::string cell_ref = base::StrCat(
          {ColumnIndexToLetters(column_index), base::NumberToString(row_number)});
      ++column_index;

      const std::string* cell_text = cell_value.GetIfString();
      if (!cell_text) {
        continue;
      }

      double numeric_value = 0;
      if (!cell_text->empty() &&
          base::StringToDouble(*cell_text, &numeric_value)) {
        base::StrAppend(&row_cells,
                        {"<c r=\"", cell_ref, "\"><v>", *cell_text,
                         "</v></c>"});
      } else {
        base::StrAppend(
            &row_cells,
            {"<c r=\"", cell_ref, "\" t=\"inlineStr\"><is><t>",
             XmlEscape(*cell_text), "</t></is></c>"});
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

}  // namespace internal

namespace {

constexpr char kContentTypesXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "content-types\">"
    "<Default Extension=\"rels\" "
    "ContentType=\"application/vnd.openxmlformats-package.relationships+"
    "xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/xl/workbook.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "spreadsheetml.sheet.main+xml\"/>"
    "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "spreadsheetml.worksheet+xml\"/>"
    "</Types>";

constexpr char kRootRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
    "</Relationships>";

constexpr char kWorkbookXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<workbook "
    "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships\">"
    "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
    "</workbook>";

constexpr char kWorkbookRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
    "</Relationships>";

}  // namespace

CreateSpreadsheetTool::CreateSpreadsheetTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreateSpreadsheetTool::~CreateSpreadsheetTool() = default;

std::string_view CreateSpreadsheetTool::Name() const {
  return mojom::kCreateSpreadsheetToolName;
}

std::string_view CreateSpreadsheetTool::Description() const {
  return "Create an Excel (.xlsx) spreadsheet from a 2D grid of cell "
         "values and download it to the user's device. Each cell is a "
         "string; values that parse as a number are stored as numbers, "
         "everything else as text. Single sheet only - no formulas, "
         "multiple sheets, or charts.";
}

std::optional<base::DictValue> CreateSpreadsheetTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".xlsx extension is added automatically).")},
       {kPropertyNameRows,
        ArrayProperty("The spreadsheet's rows, in order. Each row is an "
                      "array of cell values (as strings) for that row's "
                      "columns, left to right.",
                      ArrayProperty("A single row's cell values",
                                    StringProperty("A cell's value")))}});
}

std::optional<std::vector<std::string>>
CreateSpreadsheetTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename, kPropertyNameRows};
}

void CreateSpreadsheetTool::UseTool(const std::string& input_json,
                                    UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }

  const std::string* filename_value = input->FindString(kPropertyNameFilename);
  if (!filename_value || filename_value->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'filename'"), {});
    return;
  }

  const base::ListValue* rows = input->FindList(kPropertyNameRows);
  if (!rows || rows->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'rows' array"),
        {});
    return;
  }

  std::string filename = base::StrCat({*filename_value, ".xlsx"});

  std::vector<OoxmlPart> parts;
  parts.push_back({"[Content_Types].xml", kContentTypesXml});
  parts.push_back({"_rels/.rels", kRootRelsXml});
  parts.push_back({"xl/workbook.xml", kWorkbookXml});
  parts.push_back({"xl/_rels/workbook.xml.rels", kWorkbookRelsXml});
  parts.push_back(
      {"xl/worksheets/sheet1.xml", internal::BuildWorksheetXml(*rows)});

  BuildOoxmlArchiveAndDownload(
      browser_context_, filename, std::move(parts),
      base::BindOnce(&CreateSpreadsheetTool::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void CreateSpreadsheetTool::OnDownloadComplete(UseToolCallback callback,
                                               std::string filename,
                                               DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: failed to create '", filename,
                          "': ", result.error_message})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Created and started downloading '", filename, "'."})),
      {});
}

}  // namespace ai_chat

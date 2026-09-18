// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/export_leads_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/lead_research/lead_research_state.h"
#include "brave/browser/lead_research/lead_research_state_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyCampaignId[] = "campaign_id";

// Guards against CSV formula injection (Brave_AI_Lead_Assistant_Development_
// Blueprint.md section 14): a cell whose text starts with =, +, -, @, a tab
// or a carriage return can be interpreted as a formula by Excel/Sheets when
// the file is opened, letting an untrusted company name or note (this data
// ultimately comes from web pages/LinkedIn, not something the user typed)
// execute as a formula instead of displaying as text. Prefixing with a
// single quote neutralizes that while keeping the cell readable. Also
// applies standard CSV quoting/escaping for commas, quotes and newlines.
std::string EscapeCsvCell(const std::string& value) {
  std::string cell = value;
  if (!cell.empty()) {
    char first = cell.front();
    if (first == '=' || first == '+' || first == '-' || first == '@' ||
        first == '\t' || first == '\r') {
      cell.insert(cell.begin(), '\'');
    }
  }
  if (cell.find_first_of(",\"\n\r") == std::string::npos) {
    return cell;
  }
  std::string escaped = "\"";
  for (char c : cell) {
    if (c == '"') {
      escaped += "\"\"";
    } else {
      escaped += c;
    }
  }
  escaped += "\"";
  return escaped;
}

std::string BuildLeadsCsv(
    const std::vector<lead_research::LeadResearchState::Lead>& leads) {
  std::string csv =
      "Company,Domain,City,Service Fit,Score,Coverage %,Stage,Notes\r\n";
  for (const auto& lead : leads) {
    csv += EscapeCsvCell(lead.name);
    csv += ",";
    csv += EscapeCsvCell(lead.domain);
    csv += ",";
    csv += EscapeCsvCell(lead.city);
    csv += ",";
    csv += EscapeCsvCell(lead.service_fit);
    csv += ",";
    csv += lead.score >= 0 ? base::NumberToString(lead.score) : "";
    csv += ",";
    csv += base::NumberToString(lead.evidence_coverage_percent);
    csv += ",";
    csv += EscapeCsvCell(lead.stage);
    csv += ",";
    csv += EscapeCsvCell(lead.notes);
    csv += "\r\n";
  }
  return csv;
}
}  // namespace

ExportLeadsTool::ExportLeadsTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ExportLeadsTool::~ExportLeadsTool() = default;

std::string_view ExportLeadsTool::Name() const {
  return mojom::kExportLeadsToolName;
}

std::string_view ExportLeadsTool::Description() const {
  return "Exports a campaign's saved leads as a downloaded CSV file "
         "(company, domain, city, service fit, score, coverage, stage, "
         "notes). Downloads to the user's device, same as the other "
         "document-creating tools.";
}

std::optional<base::DictValue> ExportLeadsTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyCampaignId, StringProperty("The campaign id to export")},
  });
}

std::optional<std::vector<std::string>> ExportLeadsTool::RequiredProperties()
    const {
  return std::optional<std::vector<std::string>>({kPropertyCampaignId});
}

void ExportLeadsTool::UseTool(const std::string& input_json,
                              UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* campaign_id =
      input ? input->FindString(kPropertyCampaignId) : nullptr;
  if (!campaign_id || campaign_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'campaign_id'"), {});
    return;
  }

  auto* state = lead_research::LeadResearchStateFactory::GetForBrowserContext(
      browser_context_);
  if (!state->GetCampaign(*campaign_id)) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: no campaign found with id '",
                          *campaign_id, "'."})),
        {});
    return;
  }

  std::string csv = BuildLeadsCsv(state->GetLeads(*campaign_id));
  std::vector<uint8_t> bytes(csv.begin(), csv.end());
  std::string filename = base::StrCat({"leads_", *campaign_id, ".csv"});

  DownloadGeneratedBytes(
      browser_context_, filename, std::move(bytes),
      base::BindOnce(&ExportLeadsTool::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void ExportLeadsTool::OnDownloadComplete(UseToolCallback callback,
                                         std::string filename,
                                         DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: failed to export '", filename,
                          "': ", result.error_message})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Exported and started downloading '", filename, "'."})),
      {});
}

}  // namespace ai_chat

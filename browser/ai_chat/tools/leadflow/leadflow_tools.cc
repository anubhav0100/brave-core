// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/leadflow/leadflow_tools.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kLeadId[] = "lead_id";
constexpr char kText[] = "text";
constexpr char kTemplateName[] = "template_name";
constexpr char kLanguage[] = "language";
constexpr char kBodyParams[] = "body_params";
}  // namespace

LeadflowTool::LeadflowTool(content::BrowserContext* browser_context,
                           LeadflowOperation operation)
    : operation_(operation),
      client_(std::make_unique<leadflow::LeadFlowApiClient>(browser_context)) {}

LeadflowTool::~LeadflowTool() = default;

std::string_view LeadflowTool::Name() const {
  switch (operation_) {
    case LeadflowOperation::kAddLead:
      return mojom::kLeadflowAddLeadToolName;
    case LeadflowOperation::kSearchLeads:
      return mojom::kLeadflowSearchLeadsToolName;
    case LeadflowOperation::kUpdateLead:
      return mojom::kLeadflowUpdateLeadToolName;
    case LeadflowOperation::kSendWhatsAppMessage:
      return mojom::kLeadflowSendWhatsAppMessageToolName;
    case LeadflowOperation::kSendWhatsAppTemplate:
      return mojom::kLeadflowSendWhatsAppTemplateToolName;
    case LeadflowOperation::kGetLeadMessages:
      return mojom::kLeadflowGetLeadMessagesToolName;
  }
}

std::string_view LeadflowTool::Description() const {
  switch (operation_) {
    case LeadflowOperation::kAddLead:
      return "Creates or deduplicates a lead in the separately hosted "
             "LeadFlow CRM. WhatsApp opt-in may only be true when explicit "
             "consent evidence is supplied.";
    case LeadflowOperation::kSearchLeads:
      return "Searches LeadFlow CRM leads by name, phone, email or company.";
    case LeadflowOperation::kUpdateLead:
      return "Updates fields on one LeadFlow CRM lead.";
    case LeadflowOperation::kSendWhatsAppMessage:
      return "Sends a free-form WhatsApp message to one lead. The server "
             "enforces the customer-service window.";
    case LeadflowOperation::kSendWhatsAppTemplate:
      return "Sends an approved WhatsApp template to one opted-in lead.";
    case LeadflowOperation::kGetLeadMessages:
      return "Reads the WhatsApp message history for one LeadFlow lead.";
  }
}

std::optional<base::DictValue> LeadflowTool::InputProperties() const {
  switch (operation_) {
    case LeadflowOperation::kAddLead:
      return CreateInputProperties({
          {"first_name", StringProperty("First name")},
          {"last_name", StringProperty("Last name (optional)")},
          {"phone", StringProperty("E.164 phone number (optional if email is present)")},
          {"email", StringProperty("Email (optional if phone is present)")},
          {"company", StringProperty("Company (optional)")},
          {"city", StringProperty("City (optional)")},
          {"source",
           StringProperty(
               "Lead source",
               std::vector<std::string>{"BraveCapture", "AiAssistant",
                                        "WhatsApp", "LinkedInCapture",
                                        "GoogleMaps", "WebsiteForm"})},
          {"whatsapp_opt_in", BooleanProperty("True only with explicit consent")},
          {"consent_evidence", StringProperty("How and when consent was given")},
          {"note", StringProperty("Optional note")},
          {"tags", ArrayProperty("Optional tags", StringProperty("Tag"))},
      });
    case LeadflowOperation::kSearchLeads:
      return CreateInputProperties({
          {"query", StringProperty("Name, phone, email or company search")},
          {"status", StringProperty("Optional pipeline status")},
          {"source", StringProperty("Optional lead source")},
          {"min_score", IntegerProperty("Optional minimum score")},
          {"limit", IntegerProperty("Maximum rows, 1 to 100")},
      });
    case LeadflowOperation::kUpdateLead:
      return CreateInputProperties({
          {kLeadId, StringProperty("Lead UUID")},
          {"first_name", StringProperty("First name")},
          {"last_name", StringProperty("Last name")},
          {"email", StringProperty("Email")},
          {"company", StringProperty("Company")},
          {"city", StringProperty("City")},
          {"job_title", StringProperty("Job title")},
          {"status", StringProperty("Pipeline status")},
          {"score", IntegerProperty("Score from 0 to 100")},
          {"score_reason", StringProperty("Reason for the score")},
      });
    case LeadflowOperation::kSendWhatsAppMessage:
      return CreateInputProperties({
          {kLeadId, StringProperty("Lead UUID")},
          {kText, StringProperty("Message text")},
      });
    case LeadflowOperation::kSendWhatsAppTemplate:
      return CreateInputProperties({
          {kLeadId, StringProperty("Lead UUID")},
          {kTemplateName, StringProperty("Approved Meta template name")},
          {kLanguage, StringProperty("Template language code, e.g. en_US")},
          {kBodyParams, ArrayProperty("Template body values", StringProperty("Value"))},
      });
    case LeadflowOperation::kGetLeadMessages:
      return CreateInputProperties({{kLeadId, StringProperty("Lead UUID")}});
  }
}

std::optional<std::vector<std::string>> LeadflowTool::RequiredProperties()
    const {
  switch (operation_) {
    case LeadflowOperation::kAddLead:
      return std::vector<std::string>{"first_name", "source", "whatsapp_opt_in"};
    case LeadflowOperation::kSearchLeads:
      return std::vector<std::string>{"query"};
    case LeadflowOperation::kUpdateLead:
    case LeadflowOperation::kGetLeadMessages:
      return std::vector<std::string>{kLeadId};
    case LeadflowOperation::kSendWhatsAppMessage:
      return std::vector<std::string>{kLeadId, kText};
    case LeadflowOperation::kSendWhatsAppTemplate:
      return std::vector<std::string>{kLeadId, kTemplateName, kLanguage};
  }
}

void LeadflowTool::UseTool(const std::string& input_json,
                           UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input) {
    std::move(callback).Run(CreateContentBlocksForText("Error: invalid JSON input."), {});
    return;
  }

  const std::string* lead_id = input->FindString(kLeadId);
  std::string method = "GET";
  std::string path;
  std::optional<base::Value> body;

  switch (operation_) {
    case LeadflowOperation::kAddLead: {
      base::DictValue request = input->Clone();
      auto move_key = [&request](const char* from, const char* to) {
        if (auto value = request.Extract(from)) request.Set(to, std::move(*value));
      };
      move_key("first_name", "firstName");
      move_key("last_name", "lastName");
      move_key("whatsapp_opt_in", "whatsAppOptIn");
      move_key("consent_evidence", "consentEvidence");
      path = "/api/v1/leads";
      method = "POST";
      body = base::Value(std::move(request));
      break;
    }
    case LeadflowOperation::kSearchLeads: {
      const std::string* query = input->FindString("query");
      int limit = input->FindInt("limit").value_or(25);
      limit = std::clamp(limit, 1, 100);
      path = base::StrCat(
          {"/api/v1/leads?search=",
           base::EscapeQueryParamValue(query ? *query : "", true),
           "&pageSize=", base::NumberToString(limit)});
      if (const std::string* status = input->FindString("status")) {
        path += base::StrCat(
            {"&status=", base::EscapeQueryParamValue(*status, true)});
      }
      if (const std::string* source = input->FindString("source")) {
        path += base::StrCat(
            {"&source=", base::EscapeQueryParamValue(*source, true)});
      }
      if (std::optional<int> min_score = input->FindInt("min_score")) {
        path += base::StrCat(
            {"&minScore=", base::NumberToString(*min_score)});
      }
      break;
    }
    case LeadflowOperation::kUpdateLead: {
      if (!lead_id) break;
      base::DictValue request = input->Clone();
      request.Remove(kLeadId);
      auto move_key = [&request](const char* from, const char* to) {
        if (auto value = request.Extract(from)) request.Set(to, std::move(*value));
      };
      move_key("first_name", "firstName"); move_key("last_name", "lastName");
      move_key("job_title", "jobTitle"); move_key("score_reason", "scoreReason");
      path = base::StrCat({"/api/v1/leads/", *lead_id});
      method = "PUT";
      body = base::Value(std::move(request));
      break;
    }
    case LeadflowOperation::kSendWhatsAppMessage: {
      if (!lead_id) break;
      path = base::StrCat({"/api/v1/leads/", *lead_id, "/messages"});
      method = "POST";
      base::DictValue request;
      request.Set("text", input->FindString(kText) ? *input->FindString(kText) : "");
      body = base::Value(std::move(request));
      break;
    }
    case LeadflowOperation::kSendWhatsAppTemplate: {
      if (!lead_id) break;
      path = base::StrCat({"/api/v1/leads/", *lead_id, "/messages/template"});
      method = "POST";
      base::DictValue request;
      request.Set("templateName", input->FindString(kTemplateName) ? *input->FindString(kTemplateName) : "");
      request.Set("language", input->FindString(kLanguage) ? *input->FindString(kLanguage) : "en_US");
      if (const base::ListValue* values = input->FindList(kBodyParams)) request.Set("bodyParams", values->Clone());
      else request.Set("bodyParams", base::ListValue());
      body = base::Value(std::move(request));
      break;
    }
    case LeadflowOperation::kGetLeadMessages:
      if (lead_id) path = base::StrCat({"/api/v1/leads/", *lead_id, "/messages"});
      break;
  }

  if (path.empty()) {
    std::move(callback).Run(CreateContentBlocksForText("Error: missing or invalid lead_id."), {});
    return;
  }
  client_->Request(method, path, std::move(body),
                   base::BindOnce(&LeadflowTool::OnResult,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  std::move(callback)));
}

void LeadflowTool::OnResult(UseToolCallback callback,
                            bool,
                            base::Value result) {
  std::string json;
  if (result.is_string()) json = result.GetString();
  else base::JSONWriter::WriteWithOptions(result, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  std::move(callback).Run(CreateContentBlocksForText(json), {});
}

}  // namespace ai_chat

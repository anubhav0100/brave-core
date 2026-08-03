// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/tools/extract_document_fields_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameDocumentType[] = "document_type";
constexpr char kPropertyNameFields[] = "fields";
constexpr char kFieldPropertyNameName[] = "name";
constexpr char kFieldPropertyNameValue[] = "value";
constexpr char kFieldPropertyNameConfidence[] = "confidence";
constexpr char kConfidenceHigh[] = "high";
constexpr char kConfidenceMedium[] = "medium";
constexpr char kConfidenceLow[] = "low";

}  // namespace

ExtractDocumentFieldsTool::ExtractDocumentFieldsTool() = default;

ExtractDocumentFieldsTool::~ExtractDocumentFieldsTool() = default;

std::string_view ExtractDocumentFieldsTool::Name() const {
  return mojom::kExtractDocumentFieldsToolName;
}

std::string_view ExtractDocumentFieldsTool::Description() const {
  return "Record the structured fields you have extracted from the text of "
         "a document the user uploaded (e.g. an invoice, receipt, or "
         "application form). Call this only after you have already read "
         "the document's extracted text elsewhere in this conversation - "
         "this tool does not read the document itself, it only validates "
         "and records the fields you provide. Use short, machine-friendly "
         "field names (e.g. 'vendor_name', 'total_amount', 'due_date') so a "
         "later tool call can map them onto a web form's fields.";
}

std::optional<base::DictValue> ExtractDocumentFieldsTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameDocumentType,
        StringProperty("The kind of document this data came from, e.g. "
                       "'invoice', 'receipt', 'application_form', "
                       "'id_card', or 'generic'")},
       {kPropertyNameFields,
        ArrayProperty(
            "The structured fields you extracted from the document's text",
            ObjectProperty(
                "A single extracted field",
                {{kFieldPropertyNameName,
                  StringProperty("Machine-friendly field name, e.g. "
                                 "'vendor_name', 'total_amount', "
                                 "'due_date'")},
                 {kFieldPropertyNameValue,
                  StringProperty("The extracted value, as plain text")},
                 {kFieldPropertyNameConfidence,
                  StringProperty(
                      "How confident you are in this extraction",
                      std::vector<std::string>{kConfidenceHigh,
                                               kConfidenceMedium,
                                               kConfidenceLow})}}))}});
}

std::optional<std::vector<std::string>>
ExtractDocumentFieldsTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFields};
}

bool ExtractDocumentFieldsTool::SupportsConversation(
    bool is_temporary,
    bool has_untrusted_content,
    const ConversationCapabilitySet& conversation_capabilities) const {
  // This tool only makes sense as a step toward an agentic action (e.g.
  // filling a form from the extracted data), so it's scoped to
  // content-agent conversations rather than offered in every chat.
  return conversation_capabilities.contains(
      mojom::ConversationCapability::CONTENT_AGENT);
}

void ExtractDocumentFieldsTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: Invalid JSON input, input must be a JSON object"),
        {});
    return;
  }

  const base::ListValue* fields = input->FindList(kPropertyNameFields);
  if (!fields || fields->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Missing or empty 'fields' array"),
        {});
    return;
  }

  size_t valid_count = 0;
  for (const auto& field_value : *fields) {
    const base::DictValue* field_dict = field_value.GetIfDict();
    if (!field_dict) {
      std::move(callback).Run(
          CreateContentBlocksForText(
              "Error: Each entry in 'fields' must be an object"),
          {});
      return;
    }

    const std::string* name = field_dict->FindString(kFieldPropertyNameName);
    const std::string* value =
        field_dict->FindString(kFieldPropertyNameValue);
    if (!name || name->empty() || !value) {
      std::move(callback).Run(
          CreateContentBlocksForText(base::StrCat(
              {"Error: Field at index ", base::NumberToString(valid_count),
               " is missing a non-empty 'name' or a 'value'"})),
          {});
      return;
    }
    ++valid_count;
  }

  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Recorded ", base::NumberToString(valid_count),
           " structured field(s) from the document. You can now reference "
           "these fields by name in a subsequent tool call, such as one "
           "that maps them onto a web form."})),
      {});
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/tools/webhook_tool.h"

#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat {

WebhookTool::WebhookTool(
    std::string name,
    std::string description,
    std::string url,
    std::string secret,
    std::vector<Parameter> parameters,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : name_(std::move(name)),
      description_(std::move(description)),
      url_(std::move(url)),
      secret_(std::move(secret)),
      parameters_(std::move(parameters)) {
  static const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("ai_chat_webhook_tool", R"(
        semantics {
          sender: "AI Chat Webhook Tool"
          description:
            "Calls a webhook URL the user configured in Settings, sending "
            "the arguments the AI Assistant chose while responding to the "
            "user's request. Only ever calls webhooks the user explicitly "
            "added themselves."
          trigger:
            "The AI Assistant decides to use a user-configured webhook "
            "tool while responding in a conversation."
          data: "The tool's arguments, as chosen by the AI Assistant."
          destination: OTHER
          destination_other: "A URL the user configured in Settings."
          internal {
            contacts {
              email: "ai-chat@brave.com"
            }
          }
          user_data {
            type: NONE
          }
          last_reviewed: "2026-08-06"
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature cannot be disabled independently of AI Chat; "
            "remove the webhook tool in Settings to stop it being used."
          policy_exception_justification:
            "Not covered by a dedicated policy - users control this "
            "directly by adding or removing webhook tools themselves."
        })");
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      kTrafficAnnotation, std::move(url_loader_factory));
}

WebhookTool::~WebhookTool() = default;

std::string_view WebhookTool::Name() const {
  return name_;
}

std::string_view WebhookTool::Description() const {
  return description_;
}

std::optional<base::DictValue> WebhookTool::InputProperties() const {
  if (parameters_.empty()) {
    return std::nullopt;
  }
  base::DictValue properties;
  for (const auto& param : parameters_) {
    properties.Set(param.name, StringProperty(param.description));
  }
  return properties;
}

std::optional<std::vector<std::string>> WebhookTool::RequiredProperties()
    const {
  std::vector<std::string> required;
  for (const auto& param : parameters_) {
    if (param.required) {
      required.push_back(param.name);
    }
  }
  if (required.empty()) {
    return std::nullopt;
  }
  return required;
}

void WebhookTool::UseTool(const std::string& input_json,
                          UseToolCallback callback) {
  GURL url(url_);
  if (!url.is_valid()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: this webhook tool's URL is invalid."),
        {});
    return;
  }

  base::flat_map<std::string, std::string> headers;
  if (!secret_.empty()) {
    headers.emplace("Authorization", base::StrCat({"Bearer ", secret_}));
  }

  // input_json is already a JSON object matching this tool's schema, as
  // validated by the conversation's tool-calling machinery.
  api_request_helper_->Request(
      "POST", url, input_json, "application/json",
      base::BindOnce(&WebhookTool::OnResponse, weak_ptr_factory_.GetWeakPtr(),
                     std::move(callback)),
      headers);
}

void WebhookTool::OnResponse(UseToolCallback callback,
                             api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: the webhook returned HTTP ",
             base::NumberToString(result.response_code())})),
        {});
    return;
  }

  std::string body_text;
  if (result.value_body().is_string()) {
    body_text = result.value_body().GetString();
  } else if (!result.value_body().is_none()) {
    base::JSONWriter::Write(result.value_body(), &body_text);
  }
  if (body_text.empty()) {
    body_text = "(the webhook returned an empty response)";
  }

  std::move(callback).Run(CreateContentBlocksForText(body_text), {});
}

}  // namespace ai_chat

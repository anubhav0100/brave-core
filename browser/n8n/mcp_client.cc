// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/n8n/mcp_client.h"

#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace ai_chat {

namespace {

constexpr char kMcpSessionIdHeader[] = "Mcp-Session-Id";
constexpr char kProtocolVersion[] = "2024-11-05";

net::NetworkTrafficAnnotationTag GetMcpTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("ai_chat_mcp_client", R"(
      semantics {
        sender: "AI Chat MCP Client"
        description:
          "Talks to a local MCP (Model Context Protocol) server exposed by "
          "an n8n workflow the browser itself launched, to discover and "
          "call the tools it exposes on the user's behalf."
        trigger:
          "User asks the AI Assistant to list or use n8n-exposed MCP tools."
        data: "MCP protocol JSON-RPC messages, sent to localhost only."
        destination: LOCAL
        internal {
          contacts {
            email: "ai-chat@brave.com"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-08-09"
      }
      policy {
        cookies_allowed: NO
        setting: "This feature cannot be disabled independently of AI Chat."
        policy_exception_justification:
          "Only ever talks to a localhost MCP server exposed by a workflow "
          "running in an n8n instance the browser itself started."
      })");
}

}  // namespace

McpToolInfo::McpToolInfo() = default;
McpToolInfo::McpToolInfo(McpToolInfo&&) = default;
McpToolInfo& McpToolInfo::operator=(McpToolInfo&&) = default;
McpToolInfo::~McpToolInfo() = default;

McpClient::McpClient(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const GURL& server_url)
    : server_url_(server_url) {
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetMcpTrafficAnnotationTag(), std::move(url_loader_factory));
}

McpClient::~McpClient() = default;

void McpClient::EnsureInitialized(InitializedCallback on_ready) {
  if (initialized_) {
    std::move(on_ready).Run(true, "");
    return;
  }
  base::DictValue params;
  params.Set("protocolVersion", kProtocolVersion);
  params.Set("capabilities", base::DictValue());
  base::DictValue client_info;
  client_info.Set("name", "brave-ai-assistant");
  client_info.Set("version", "1.0");
  params.Set("clientInfo", std::move(client_info));

  base::DictValue body;
  body.Set("jsonrpc", "2.0");
  body.Set("id", next_request_id_++);
  body.Set("method", "initialize");
  body.Set("params", std::move(params));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  base::flat_map<std::string, std::string> headers;
  headers.emplace("Accept", "application/json, text/event-stream");
  api_request_helper_->Request(
      "POST", server_url_, body_json, "application/json",
      base::BindOnce(&McpClient::OnInitializeResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(on_ready)),
      headers);
}

void McpClient::OnInitializeResponse(
    InitializedCallback on_ready,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(on_ready).Run(
        false, base::StrCat({"MCP initialize failed with HTTP ",
                             base::NumberToString(result.response_code())}));
    return;
  }
  auto it = result.headers().find(kMcpSessionIdHeader);
  if (it != result.headers().end()) {
    session_id_ = it->second;
  }
  initialized_ = true;

  // MCP requires the client to send an "initialized" notification
  // (no id, no response expected) before making further calls.
  base::DictValue notification;
  notification.Set("jsonrpc", "2.0");
  notification.Set("method", "notifications/initialized");
  std::string notification_json;
  base::JSONWriter::Write(notification, &notification_json);

  base::flat_map<std::string, std::string> headers;
  headers.emplace("Accept", "application/json, text/event-stream");
  if (!session_id_.empty()) {
    headers.emplace(kMcpSessionIdHeader, session_id_);
  }
  api_request_helper_->Request(
      "POST", server_url_, notification_json, "application/json",
      base::BindOnce(
          [](InitializedCallback on_ready,
             api_request_helper::APIRequestResult) {
            std::move(on_ready).Run(true, "");
          },
          std::move(on_ready)),
      headers);
}

void McpClient::SendJsonRpc(const std::string& method,
                            base::DictValue params,
                            JsonRpcCallback callback) {
  base::DictValue body;
  body.Set("jsonrpc", "2.0");
  body.Set("id", next_request_id_++);
  body.Set("method", method);
  body.Set("params", std::move(params));

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  base::flat_map<std::string, std::string> headers;
  headers.emplace("Accept", "application/json, text/event-stream");
  if (!session_id_.empty()) {
    headers.emplace(kMcpSessionIdHeader, session_id_);
  }
  api_request_helper_->Request(
      "POST", server_url_, body_json, "application/json",
      base::BindOnce(&McpClient::OnJsonRpcResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void McpClient::OnJsonRpcResponse(
    JsonRpcCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        false,
        base::StrCat({"MCP request failed with HTTP ",
                      base::NumberToString(result.response_code())}),
        base::Value());
    return;
  }
  const base::Value& body = result.value_body();
  if (!body.is_dict()) {
    std::move(callback).Run(false, "MCP server returned a non-JSON response",
                            base::Value());
    return;
  }
  const base::DictValue& dict = body.GetDict();
  if (const base::DictValue* error = dict.FindDict("error")) {
    const std::string* message = error->FindString("message");
    std::move(callback).Run(
        false,
        base::StrCat({"MCP error: ", message ? *message : "unknown error"}),
        base::Value());
    return;
  }
  const base::Value* mcp_result = dict.Find("result");
  if (!mcp_result) {
    std::move(callback).Run(false, "MCP response had no 'result'",
                            base::Value());
    return;
  }
  std::move(callback).Run(true, "", mcp_result->Clone());
}

void McpClient::ListTools(ListToolsCallback callback) {
  EnsureInitialized(base::BindOnce(&McpClient::OnListToolsInitialized,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(callback)));
}

void McpClient::OnListToolsInitialized(ListToolsCallback callback,
                                       bool success,
                                       std::string error) {
  if (!success) {
    std::move(callback).Run(false, error, {});
    return;
  }
  SendJsonRpc("tools/list", base::DictValue(),
             base::BindOnce(&McpClient::OnListToolsResult,
                            weak_ptr_factory_.GetWeakPtr(),
                            std::move(callback)));
}

void McpClient::OnListToolsResult(ListToolsCallback callback,
                                  bool success,
                                  std::string error,
                                  base::Value result) {
  if (!success) {
    std::move(callback).Run(false, error, {});
    return;
  }
  std::vector<McpToolInfo> tools;
  if (result.is_dict()) {
    if (const base::ListValue* tool_list = result.GetDict().FindList("tools")) {
      for (const auto& tool_value : *tool_list) {
        if (!tool_value.is_dict()) {
          continue;
        }
        const base::DictValue& tool_dict = tool_value.GetDict();
        McpToolInfo info;
        if (const std::string* name = tool_dict.FindString("name")) {
          info.name = *name;
        }
        if (const std::string* description =
                tool_dict.FindString("description")) {
          info.description = *description;
        }
        if (const base::DictValue* schema =
                tool_dict.FindDict("inputSchema")) {
          info.input_schema = schema->Clone();
        }
        if (!info.name.empty()) {
          tools.push_back(std::move(info));
        }
      }
    }
  }
  std::move(callback).Run(true, "", std::move(tools));
}

void McpClient::CallTool(const std::string& tool_name,
                         const base::DictValue& arguments,
                         CallToolCallback callback) {
  EnsureInitialized(base::BindOnce(
      &McpClient::OnCallToolInitialized, weak_ptr_factory_.GetWeakPtr(),
      tool_name, arguments.Clone(), std::move(callback)));
}

void McpClient::OnCallToolInitialized(std::string tool_name,
                                      base::DictValue arguments,
                                      CallToolCallback callback,
                                      bool success,
                                      std::string error) {
  if (!success) {
    std::move(callback).Run(false, error);
    return;
  }
  base::DictValue params;
  params.Set("name", tool_name);
  params.Set("arguments", std::move(arguments));
  SendJsonRpc("tools/call", std::move(params),
             base::BindOnce(&McpClient::OnCallToolResult,
                            weak_ptr_factory_.GetWeakPtr(),
                            std::move(callback)));
}

void McpClient::OnCallToolResult(CallToolCallback callback,
                                 bool success,
                                 std::string error,
                                 base::Value result) {
  if (!success) {
    std::move(callback).Run(false, error);
    return;
  }
  // MCP tool results are {"content": [{"type": "text", "text": "..."}, ...]}
  // - concatenate the text blocks; non-text content (images, etc.) is
  // summarized rather than dropped silently.
  std::string text;
  if (result.is_dict()) {
    if (const base::ListValue* content =
            result.GetDict().FindList("content")) {
      for (const auto& block : *content) {
        if (!block.is_dict()) {
          continue;
        }
        const std::string* type = block.GetDict().FindString("type");
        if (type && *type == "text") {
          if (const std::string* text_value =
                  block.GetDict().FindString("text")) {
            if (!text.empty()) {
              text += "\n";
            }
            text += *text_value;
          }
        } else if (type) {
          text += base::StrCat({"[", *type, " content omitted]\n"});
        }
      }
    }
  }
  if (text.empty()) {
    base::JSONWriter::Write(result, &text);
  }
  std::move(callback).Run(true, text);
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_N8N_MCP_CLIENT_H_
#define BRAVE_BROWSER_N8N_MCP_CLIENT_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "url/gurl.h"

namespace api_request_helper {
class APIRequestHelper;
class APIRequestResult;
}  // namespace api_request_helper

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace ai_chat {

// One tool exposed by an MCP server, as returned by its tools/list.
struct McpToolInfo {
  McpToolInfo();
  McpToolInfo(McpToolInfo&&);
  McpToolInfo& operator=(McpToolInfo&&);
  ~McpToolInfo();

  std::string name;
  std::string description;
  base::DictValue input_schema;
};

// A minimal client for one MCP server, speaking the Streamable HTTP
// transport (a single POST endpoint, JSON-RPC 2.0 - see
// https://modelcontextprotocol.io/specification) - built against n8n's
// MCP Server Trigger node, which exposes exactly this at
// <n8n base url>/mcp/<workflow's webhook path> for an activated workflow.
//
// Each instance runs its own fresh "initialize" handshake before its first
// real call and doesn't persist a session across separate McpClient
// instances - simpler than caching a client (and its session) per server
// across tool calls, at the cost of one extra round trip per use. The MCP
// spec treats the session id servers may return as optional for clients to
// carry forward; this hasn't been stress-tested against n8n for very
// chatty multi-call sequences, only single list/call operations.
class McpClient {
 public:
  using ListToolsCallback =
      base::OnceCallback<void(bool success,
                              std::string error,
                              std::vector<McpToolInfo> tools)>;
  using CallToolCallback =
      base::OnceCallback<void(bool success, std::string result_text)>;

  McpClient(scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
           const GURL& server_url);
  ~McpClient();

  McpClient(const McpClient&) = delete;
  McpClient& operator=(const McpClient&) = delete;

  void ListTools(ListToolsCallback callback);
  void CallTool(const std::string& tool_name,
               const base::DictValue& arguments,
               CallToolCallback callback);

 private:
  using JsonRpcCallback =
      base::OnceCallback<void(bool success,
                              std::string error,
                              base::Value result)>;
  using InitializedCallback = base::OnceCallback<void(bool success,
                                                       std::string error)>;

  // Runs the MCP "initialize" handshake if it hasn't already succeeded on
  // this instance, then invokes `on_ready` with the outcome.
  void EnsureInitialized(InitializedCallback on_ready);
  void OnInitializeResponse(InitializedCallback on_ready,
                            api_request_helper::APIRequestResult result);

  void SendJsonRpc(const std::string& method,
                   base::DictValue params,
                   JsonRpcCallback callback);
  void OnJsonRpcResponse(JsonRpcCallback callback,
                        api_request_helper::APIRequestResult result);

  void OnListToolsInitialized(ListToolsCallback callback,
                             bool success,
                             std::string error);
  void OnListToolsResult(ListToolsCallback callback,
                        bool success,
                        std::string error,
                        base::Value result);
  void OnCallToolInitialized(std::string tool_name,
                            base::DictValue arguments,
                            CallToolCallback callback,
                            bool success,
                            std::string error);
  void OnCallToolResult(CallToolCallback callback,
                       bool success,
                       std::string error,
                       base::Value result);

  GURL server_url_;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;
  bool initialized_ = false;
  std::string session_id_;
  int next_request_id_ = 1;

  base::WeakPtrFactory<McpClient> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_N8N_MCP_CLIENT_H_

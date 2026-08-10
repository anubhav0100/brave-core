// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_MCP_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_MCP_TOOLS_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class McpClient;

// Lists the MCP tools currently exposed by this profile's n8n instance -
// every *activated* workflow containing an "MCP Server Trigger" node (see
// https://docs.n8n.io/integrations/builtin/core-nodes/n8n-nodes-langchain.mcptrigger/),
// with the tools each one exposes (from its own tools/list). Call this to
// see what's available before call_n8n_mcp_tool. Starts n8n first if it
// isn't already running; requires an API key (see SetN8nApiKeyTool).
//
// A workflow only shows up here once it's activated - see
// activate_n8n_workflow (browser/ai_chat/tools/n8n_tools.h) - and only
// exposes tools while n8n itself is running.
class ListN8nMcpToolsTool : public Tool {
 public:
  ListN8nMcpToolsTool(N8nProcessManager* manager,
                      content::BrowserContext* browser_context);
  ~ListN8nMcpToolsTool() override;

  ListN8nMcpToolsTool(const ListN8nMcpToolsTool&) = delete;
  ListN8nMcpToolsTool& operator=(const ListN8nMcpToolsTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(UseToolCallback callback, bool success);
  void OnWorkflowsListed(UseToolCallback callback,
                        bool success,
                        std::string error_message,
                        std::vector<N8nProcessManager::McpWorkflowInfo>
                            workflows);
  void OnAllServersListed(UseToolCallback callback,
                         std::vector<std::string> server_labels,
                         std::vector<std::unique_ptr<McpClient>> clients);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<ListN8nMcpToolsTool> weak_ptr_factory_{this};
};

// Calls one tool exposed by one of this profile's activated n8n MCP-trigger
// workflows - see ListN8nMcpToolsTool first, to find the workflow name and
// tool name to use here. Starts n8n first if it isn't already running;
// requires an API key (see SetN8nApiKeyTool).
class CallN8nMcpToolTool : public Tool {
 public:
  CallN8nMcpToolTool(N8nProcessManager* manager,
                     content::BrowserContext* browser_context);
  ~CallN8nMcpToolTool() override;

  CallN8nMcpToolTool(const CallN8nMcpToolTool&) = delete;
  CallN8nMcpToolTool& operator=(const CallN8nMcpToolTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(std::string workflow_name,
                std::string tool_name,
                std::string arguments_json,
                UseToolCallback callback,
                bool success);
  void OnWorkflowsListedForCall(
      std::string workflow_name,
      std::string tool_name,
      base::DictValue arguments,
      UseToolCallback callback,
      bool success,
      std::string error_message,
      std::vector<N8nProcessManager::McpWorkflowInfo> workflows);
  void OnToolCalled(std::unique_ptr<McpClient> client,
                    UseToolCallback callback,
                    bool success,
                    std::string result_text);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<CallN8nMcpToolTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_MCP_TOOLS_H_

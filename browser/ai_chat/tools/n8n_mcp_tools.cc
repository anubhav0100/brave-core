// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/n8n_mcp_tools.h"

#include <algorithm>
#include <utility>

#include "base/barrier_closure.h"
#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/n8n/mcp_client.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameWorkflowName[] = "workflow_name";
constexpr char kPropertyNameToolName[] = "tool_name";
constexpr char kPropertyNameArguments[] = "arguments";

constexpr char kMcpTriggerNodeType[] = "@n8n/n8n-nodes-langchain.mcpTrigger";

constexpr char kNoApiKeyError[] =
    "Error: no n8n API key stored yet. Ask the user to: 1) open n8n (call "
    "open_n8n if it's not already open), 2) complete its one-time owner "
    "account setup if this is a fresh instance, 3) go to Settings > n8n "
    "API and generate a key, 4) tell you the key so you can call "
    "set_n8n_api_key with it.";

net::NetworkTrafficAnnotationTag GetN8nTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("ai_chat_n8n_mcp_discovery", R"(
      semantics {
        sender: "AI Chat n8n MCP Tools"
        description:
          "Lists the user's local n8n workflows to find which ones expose "
          "an MCP Server Trigger, so the AI Assistant can discover and use "
          "the tools they expose."
        trigger:
          "User asks the AI Assistant to list or use n8n MCP tools."
        data: "None sent beyond an API key header; localhost only."
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
          "Only ever talks to a localhost n8n instance the browser itself "
          "started."
      })");
}

// One workflow found to be an active MCP server, with the URL its MCP
// endpoint is reachable at.
struct McpWorkflow {
  std::string name;
  GURL mcp_url;
};

// Extracts every activated workflow with an MCP Server Trigger node from
// n8n's `GET /api/v1/workflows` response - which may be either
// `{"data": [...]}` (the documented Public API shape) or a bare array,
// depending on n8n version, so both are accepted defensively.
std::vector<McpWorkflow> ExtractMcpWorkflows(const base::Value& body,
                                             const std::string& base_url) {
  std::vector<McpWorkflow> result;
  const base::ListValue* list = nullptr;
  if (body.is_dict()) {
    list = body.GetDict().FindList("data");
  } else if (body.is_list()) {
    list = &body.GetList();
  }
  if (!list) {
    return result;
  }
  for (const auto& workflow_value : *list) {
    if (!workflow_value.is_dict()) {
      continue;
    }
    const base::DictValue& workflow = workflow_value.GetDict();
    if (!workflow.FindBool("active").value_or(false)) {
      continue;
    }
    const base::ListValue* nodes = workflow.FindList("nodes");
    if (!nodes) {
      continue;
    }
    for (const auto& node_value : *nodes) {
      if (!node_value.is_dict()) {
        continue;
      }
      const base::DictValue& node = node_value.GetDict();
      const std::string* type = node.FindString("type");
      if (!type || *type != kMcpTriggerNodeType) {
        continue;
      }
      std::string path;
      if (const base::DictValue* params = node.FindDict("parameters")) {
        if (const std::string* path_param = params->FindString("path")) {
          path = *path_param;
        }
      }
      if (path.empty()) {
        if (const std::string* webhook_id = node.FindString("webhookId")) {
          path = *webhook_id;
        }
      }
      if (path.empty()) {
        continue;
      }
      const std::string* name = workflow.FindString("name");
      result.push_back(
          {name ? *name : "(unnamed workflow)",
           GURL(base::StrCat({base_url, "/mcp/", path}))});
      break;  // Only one MCP trigger per workflow is meaningful.
    }
  }
  return result;
}

}  // namespace

// ListN8nMcpToolsTool --------------------------------------------------

ListN8nMcpToolsTool::ListN8nMcpToolsTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

ListN8nMcpToolsTool::~ListN8nMcpToolsTool() = default;

std::string_view ListN8nMcpToolsTool::Name() const {
  return "list_n8n_mcp_tools";
}

std::string_view ListN8nMcpToolsTool::Description() const {
  return "Lists the MCP tools currently exposed by this profile's n8n "
         "instance - every activated workflow with an MCP Server Trigger "
         "node, and the tools each one exposes. Call this before "
         "call_n8n_mcp_tool to see what's available. Starts n8n first if "
         "it isn't already running.";
}

void ListN8nMcpToolsTool::UseTool(const std::string& input_json,
                                  UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(&ListN8nMcpToolsTool::OnStarted,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         std::move(callback)));
}

void ListN8nMcpToolsTool::OnStarted(UseToolCallback callback, bool success) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n failed to start."), {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key = prefs ? N8nProcessManager::GetApiKey(prefs) : "";
  if (api_key.empty()) {
    std::move(callback).Run(CreateContentBlocksForText(kNoApiKeyError), {});
    return;
  }
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "GET", GURL(base::StrCat({manager_->base_url(), "/api/v1/workflows"})),
      "", "",
      base::BindOnce(&ListN8nMcpToolsTool::OnWorkflowsListed,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void ListN8nMcpToolsTool::OnWorkflowsListed(
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()),
             " listing workflows."})),
        {});
    return;
  }
  std::vector<McpWorkflow> workflows =
      ExtractMcpWorkflows(result.value_body(), manager_->base_url());
  if (workflows.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "No activated workflows with an MCP Server Trigger node found. "
            "Create one with create_n8n_workflow (include an "
            "@n8n/n8n-nodes-langchain.mcpTrigger node) or build one in "
            "n8n's editor (open_n8n), then activate it with "
            "activate_n8n_workflow."),
        {});
    return;
  }

  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  auto results = std::make_unique<std::vector<std::pair<std::string, std::string>>>();
  auto* results_ptr = results.get();
  auto clients =
      std::make_unique<std::vector<std::unique_ptr<McpClient>>>();
  auto* clients_ptr = clients.get();
  std::vector<std::string> labels;
  for (const auto& workflow : workflows) {
    labels.push_back(workflow.name);
  }

  base::RepeatingClosure barrier = base::BarrierClosure(
      workflows.size(),
      base::BindOnce(
          [](UseToolCallback callback,
             std::unique_ptr<std::vector<std::pair<std::string, std::string>>>
                 results,
             std::unique_ptr<std::vector<std::unique_ptr<McpClient>>>) {
            std::string text = "MCP tools available via n8n:\n\n";
            for (const auto& [label, tools_text] : *results) {
              base::StrAppend(&text, {label, ":\n", tools_text, "\n"});
            }
            std::move(callback).Run(CreateContentBlocksForText(text), {});
          },
          std::move(callback), std::move(results), std::move(clients)));

  for (const auto& workflow : workflows) {
    auto client =
        std::make_unique<McpClient>(url_loader_factory, workflow.mcp_url);
    McpClient* client_ptr = client.get();
    clients_ptr->push_back(std::move(client));
    std::string label = workflow.name;
    client_ptr->ListTools(base::BindOnce(
        [](std::string label,
           std::vector<std::pair<std::string, std::string>>* results,
           base::RepeatingClosure barrier, bool success, std::string error,
           std::vector<McpToolInfo> tools) {
          std::string text;
          if (!success) {
            text = base::StrCat({"  (unavailable: ", error, ")"});
          } else if (tools.empty()) {
            text = "  (no tools exposed)";
          } else {
            for (const auto& tool : tools) {
              base::StrAppend(&text, {"  - ", tool.name, ": ",
                                      tool.description, "\n"});
            }
          }
          results->emplace_back(std::move(label), std::move(text));
          barrier.Run();
        },
        label, results_ptr, barrier));
  }
}

// CallN8nMcpToolTool -----------------------------------------------------

CallN8nMcpToolTool::CallN8nMcpToolTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

CallN8nMcpToolTool::~CallN8nMcpToolTool() = default;

std::string_view CallN8nMcpToolTool::Name() const {
  return "call_n8n_mcp_tool";
}

std::string_view CallN8nMcpToolTool::Description() const {
  return "Calls one tool exposed by one of this profile's activated n8n "
         "MCP-trigger workflows. Call list_n8n_mcp_tools first to find the "
         "workflow name and tool name to use. Starts n8n first if it "
         "isn't already running.";
}

std::optional<base::DictValue> CallN8nMcpToolTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowName,
        StringProperty("The workflow's name, as shown by "
                       "list_n8n_mcp_tools.")},
       {kPropertyNameToolName, StringProperty("The tool's name.")},
       {kPropertyNameArguments,
        StringProperty("JSON object of arguments for the tool, matching "
                       "its input schema from list_n8n_mcp_tools. Use "
                       "'{}' if it takes none.")}});
}

std::optional<std::vector<std::string>>
CallN8nMcpToolTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameWorkflowName,
                                  kPropertyNameToolName,
                                  kPropertyNameArguments};
}

void CallN8nMcpToolTool::UseTool(const std::string& input_json,
                                 UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const std::string* workflow_name =
      input->FindString(kPropertyNameWorkflowName);
  const std::string* tool_name = input->FindString(kPropertyNameToolName);
  const std::string* arguments_json =
      input->FindString(kPropertyNameArguments);
  if (!workflow_name || !tool_name || !arguments_json) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing 'workflow_name', 'tool_name', or 'arguments'"),
        {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &CallN8nMcpToolTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      *workflow_name, *tool_name, *arguments_json, std::move(callback)));
}

void CallN8nMcpToolTool::OnStarted(std::string workflow_name,
                                   std::string tool_name,
                                   std::string arguments_json,
                                   UseToolCallback callback,
                                   bool success) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n failed to start."), {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key = prefs ? N8nProcessManager::GetApiKey(prefs) : "";
  if (api_key.empty()) {
    std::move(callback).Run(CreateContentBlocksForText(kNoApiKeyError), {});
    return;
  }
  auto arguments_value = base::JSONReader::Read(
      arguments_json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!arguments_value || !arguments_value->is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: 'arguments' must be a JSON object."),
        {});
    return;
  }
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "GET", GURL(base::StrCat({manager_->base_url(), "/api/v1/workflows"})),
      "", "",
      base::BindOnce(&CallN8nMcpToolTool::OnWorkflowsListedForCall,
                     weak_ptr_factory_.GetWeakPtr(), workflow_name, tool_name,
                     std::move(arguments_value->GetDict()),
                     std::move(callback)),
      headers);
}

void CallN8nMcpToolTool::OnWorkflowsListedForCall(
    std::string workflow_name,
    std::string tool_name,
    base::DictValue arguments,
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()),
             " listing workflows."})),
        {});
    return;
  }
  std::vector<McpWorkflow> workflows =
      ExtractMcpWorkflows(result.value_body(), manager_->base_url());
  auto it = std::ranges::find(workflows, workflow_name, &McpWorkflow::name);
  if (it == workflows.end()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: no activated MCP-trigger workflow named '",
             workflow_name,
             "' found. Call list_n8n_mcp_tools to see what's available."})),
        {});
    return;
  }
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  auto client = std::make_unique<McpClient>(url_loader_factory, it->mcp_url);
  McpClient* client_ptr = client.get();
  client_ptr->CallTool(
      tool_name, arguments,
      base::BindOnce(&CallN8nMcpToolTool::OnToolCalled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(client),
                     std::move(callback)));
}

void CallN8nMcpToolTool::OnToolCalled(std::unique_ptr<McpClient> client,
                                      UseToolCallback callback,
                                      bool success,
                                      std::string result_text) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat({"Error: ", result_text})),
        {});
    return;
  }
  std::move(callback).Run(CreateContentBlocksForText(result_text), {});
}

}  // namespace ai_chat

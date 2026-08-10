// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/n8n_tools.h"

#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameName[] = "name";
constexpr char kPropertyNameNodes[] = "nodes";
constexpr char kPropertyNameConnections[] = "connections";
constexpr char kPropertyNameWorkflowId[] = "workflow_id";
constexpr char kPropertyNameApiKey[] = "api_key";
constexpr char kPropertyNameVersionTimestamp[] = "version_timestamp";

// Builds the {name, nodes, connections, settings} body n8n's Public API
// accepts for both creating and updating a workflow (same shape - see
// CreateN8nWorkflowTool, which this mirrors and whose shape was verified
// live against a real n8n instance for the create case).
std::string BuildWorkflowBodyJson(const std::string& name,
                                  base::Value nodes,
                                  base::Value connections) {
  base::DictValue body;
  body.Set("name", name);
  body.Set("nodes", std::move(nodes));
  body.Set("connections", std::move(connections));
  body.Set("settings", base::DictValue());
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);
  return body_json;
}

constexpr char kNoApiKeyError[] =
    "Error: no n8n API key stored yet. Ask the user to: 1) open n8n (call "
    "open_n8n if it's not already open), 2) complete its one-time owner "
    "account setup if this is a fresh instance, 3) go to Settings > n8n "
    "API and generate a key, 4) tell you the key so you can call "
    "set_n8n_api_key with it.";

net::NetworkTrafficAnnotationTag GetN8nTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("ai_chat_n8n_tool", R"(
      semantics {
        sender: "AI Chat n8n Tool"
        description:
          "Talks to a local n8n instance the browser itself launched, to "
          "create or run automation workflows on the user's behalf."
        trigger:
          "User asks the AI Assistant to create or run an n8n workflow."
        data: "Workflow definitions and run parameters, sent to localhost."
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
          "started, never a remote destination."
      })");
}

}  // namespace

// OpenN8nTool ---------------------------------------------------------------

OpenN8nTool::OpenN8nTool(N8nProcessManager* manager,
                         content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {}

OpenN8nTool::~OpenN8nTool() = default;

std::string_view OpenN8nTool::Name() const {
  return "open_n8n";
}

std::string_view OpenN8nTool::Description() const {
  return "Starts the local n8n workflow-automation instance if it isn't "
         "already running, and opens its editor UI in a new tab. Use this "
         "when the user wants to build or edit a flow visually rather than "
         "have you generate its JSON definition directly with "
         "create_n8n_workflow.";
}

void OpenN8nTool::UseTool(const std::string& input_json,
                          UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &OpenN8nTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      std::move(callback)));
}

void OpenN8nTool::OnStarted(UseToolCallback callback, bool success) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: n8n failed to start. It's launched via `npx n8n` - "
            "make sure Node.js is installed and reachable."),
        {});
    return;
  }
  content::WebContents* web_contents = nullptr;
  if (Profile* profile = Profile::FromBrowserContext(browser_context_)) {
    if (BrowserWindowInterface* browser =
            ProfileBrowserCollection::GetForProfile(profile)
                ->FindTabbedBrowser()) {
      if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
        web_contents = tab->GetContents();
      }
    }
  }
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no open browser window to open "
                                   "a tab in."),
        {});
    return;
  }
  web_contents->OpenURL(
      {GURL(manager_->base_url()), content::Referrer(),
       WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK,
       /*is_renderer_initiated=*/false},
      /*navigation_handle_callback=*/{});
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Opened n8n at ", manager_->base_url(), "."})),
      {});
}

// SetN8nApiKeyTool ------------------------------------------------------

SetN8nApiKeyTool::SetN8nApiKeyTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

SetN8nApiKeyTool::~SetN8nApiKeyTool() = default;

std::string_view SetN8nApiKeyTool::Name() const {
  return "set_n8n_api_key";
}

std::string_view SetN8nApiKeyTool::Description() const {
  return "Stores the n8n Public API key the user generated via n8n's own "
         "UI (Settings > n8n API). Required once before "
         "create_n8n_workflow or run_n8n_workflow will work.";
}

std::optional<base::DictValue> SetN8nApiKeyTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameApiKey, StringProperty("The n8n API key.")}});
}

std::optional<std::vector<std::string>>
SetN8nApiKeyTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameApiKey};
}

void SetN8nApiKeyTool::UseTool(const std::string& input_json,
                               UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* api_key =
      input.has_value() ? input->FindString(kPropertyNameApiKey) : nullptr;
  if (!api_key || api_key->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'api_key'"), {});
    return;
  }
  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  if (!prefs) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no profile to store this in."),
        {});
    return;
  }
  N8nProcessManager::SetApiKey(prefs, *api_key);
  std::move(callback).Run(
      CreateContentBlocksForText("Saved the n8n API key."), {});
}

// CreateN8nWorkflowTool ------------------------------------------------------

CreateN8nWorkflowTool::CreateN8nWorkflowTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

CreateN8nWorkflowTool::~CreateN8nWorkflowTool() = default;

std::string_view CreateN8nWorkflowTool::Name() const {
  return "create_n8n_workflow";
}

std::string_view CreateN8nWorkflowTool::Description() const {
  return "Creates a new n8n workflow from its node/connection graph (n8n's "
         "own workflow JSON format - the same shape its editor saves/loads; "
         "see https://docs.n8n.io/workflows/ for the schema). Starts n8n "
         "first if it isn't already running. The created workflow starts "
         "inactive - open it with open_n8n or call run_n8n_workflow to "
         "execute it once. Building simple, common flows (webhook -> "
         "action, schedule -> action) is reliable; for anything complex, "
         "prefer telling the user to build it visually via open_n8n.";
}

std::optional<base::DictValue> CreateN8nWorkflowTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameName, StringProperty("The workflow's name.")},
       {kPropertyNameNodes,
        StringProperty(
            "JSON array of n8n node objects (the workflow's \"nodes\" "
            "field) - each with at least id, name, type, typeVersion, "
            "position, and parameters.")},
       {kPropertyNameConnections,
        StringProperty(
            "JSON object of n8n's \"connections\" field, mapping each "
            "source node's name to its outgoing connections.")}});
}

std::optional<std::vector<std::string>>
CreateN8nWorkflowTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameName, kPropertyNameNodes,
                                  kPropertyNameConnections};
}

void CreateN8nWorkflowTool::UseTool(const std::string& input_json,
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
  const std::string* name = input->FindString(kPropertyNameName);
  const std::string* nodes = input->FindString(kPropertyNameNodes);
  const std::string* connections =
      input->FindString(kPropertyNameConnections);
  if (!name || !nodes || !connections) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing 'name', 'nodes', or 'connections'"),
        {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &CreateN8nWorkflowTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      *name, *nodes, *connections, std::move(callback)));
}

void CreateN8nWorkflowTool::OnStarted(std::string name,
                                      std::string nodes_json,
                                      std::string connections_json,
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
  auto nodes = base::JSONReader::Read(nodes_json,
                                      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  auto connections = base::JSONReader::Read(
      connections_json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!nodes || !nodes->is_list() || !connections ||
      !connections->is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: 'nodes' must be a JSON array and 'connections' a JSON "
            "object."),
        {});
    return;
  }

  base::DictValue body;
  body.Set("name", name);
  body.Set("nodes", std::move(*nodes));
  body.Set("connections", std::move(*connections));
  body.Set("settings", base::DictValue());

  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "POST",
      GURL(base::StrCat({manager_->base_url(), "/api/v1/workflows"})),
      body_json, "application/json",
      base::BindOnce(&CreateN8nWorkflowTool::OnCreateResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void CreateN8nWorkflowTool::OnCreateResponse(
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::string body_text;
    base::JSONWriter::Write(result.value_body(), &body_text);
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()), ": ",
             body_text})),
        {});
    return;
  }
  std::string body_text;
  base::JSONWriter::Write(result.value_body(), &body_text);
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Created workflow. Response: ", body_text})),
      {});
}

// RunN8nWorkflowTool ---------------------------------------------------------

RunN8nWorkflowTool::RunN8nWorkflowTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

RunN8nWorkflowTool::~RunN8nWorkflowTool() = default;

std::string_view RunN8nWorkflowTool::Name() const {
  return "activate_n8n_workflow";
}

std::string_view RunN8nWorkflowTool::Description() const {
  return "Activates an existing n8n workflow by id (the id returned from "
         "create_n8n_workflow, or visible in n8n's editor URL), so it "
         "starts running on its own Schedule/Webhook/other trigger going "
         "forward. Starts n8n first if it isn't already running. Only "
         "works for workflows that have a real trigger node - a workflow "
         "with just a Manual Trigger can't be activated this way (n8n has "
         "no API for one-off manual runs); tell the user to run it "
         "themselves from n8n's editor instead (open_n8n).";
}

std::optional<base::DictValue> RunN8nWorkflowTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowId, StringProperty("The workflow's id.")}});
}

std::optional<std::vector<std::string>>
RunN8nWorkflowTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameWorkflowId};
}

void RunN8nWorkflowTool::UseTool(const std::string& input_json,
                                 UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* workflow_id =
      input.has_value() ? input->FindString(kPropertyNameWorkflowId)
                        : nullptr;
  if (!workflow_id || workflow_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'workflow_id'"), {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &RunN8nWorkflowTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      *workflow_id, std::move(callback)));
}

void RunN8nWorkflowTool::OnStarted(std::string workflow_id,
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
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "POST",
      GURL(base::StrCat({manager_->base_url(), "/api/v1/workflows/",
                         workflow_id, "/activate"})),
      "", "application/json",
      base::BindOnce(&RunN8nWorkflowTool::OnRunResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void RunN8nWorkflowTool::OnRunResponse(
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  std::string body_text;
  base::JSONWriter::Write(result.value_body(), &body_text);
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()), ": ",
             body_text})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Activated workflow. ", body_text})),
      {});
}

// UpdateN8nWorkflowTool -------------------------------------------------

UpdateN8nWorkflowTool::UpdateN8nWorkflowTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

UpdateN8nWorkflowTool::~UpdateN8nWorkflowTool() = default;

std::string_view UpdateN8nWorkflowTool::Name() const {
  return "update_n8n_workflow";
}

std::string_view UpdateN8nWorkflowTool::Description() const {
  return "Updates an existing n8n workflow's node/connection graph by id. "
         "Its current definition is automatically saved to local version "
         "history before being overwritten (see list_n8n_workflow_versions "
         "and rollback_n8n_workflow), so mistakes are always recoverable. "
         "Starts n8n first if it isn't already running.";
}

std::optional<base::DictValue> UpdateN8nWorkflowTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowId, StringProperty("The workflow's id.")},
       {kPropertyNameName, StringProperty("The workflow's name.")},
       {kPropertyNameNodes,
        StringProperty("JSON array of n8n node objects - the full, "
                       "updated \"nodes\" field.")},
       {kPropertyNameConnections,
        StringProperty("JSON object of n8n's \"connections\" field - the "
                       "full, updated connection graph.")}});
}

std::optional<std::vector<std::string>>
UpdateN8nWorkflowTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameWorkflowId, kPropertyNameName,
                                  kPropertyNameNodes,
                                  kPropertyNameConnections};
}

void UpdateN8nWorkflowTool::UseTool(const std::string& input_json,
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
  const std::string* workflow_id = input->FindString(kPropertyNameWorkflowId);
  const std::string* name = input->FindString(kPropertyNameName);
  const std::string* nodes = input->FindString(kPropertyNameNodes);
  const std::string* connections =
      input->FindString(kPropertyNameConnections);
  if (!workflow_id || !name || !nodes || !connections) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing 'workflow_id', 'name', 'nodes', or "
            "'connections'"),
        {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &UpdateN8nWorkflowTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      *workflow_id, *name, *nodes, *connections, std::move(callback)));
}

void UpdateN8nWorkflowTool::OnStarted(std::string workflow_id,
                                      std::string name,
                                      std::string nodes_json,
                                      std::string connections_json,
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
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "GET",
      GURL(base::StrCat(
          {manager_->base_url(), "/api/v1/workflows/", workflow_id})),
      "", "",
      base::BindOnce(&UpdateN8nWorkflowTool::OnCurrentWorkflowFetched,
                     weak_ptr_factory_.GetWeakPtr(), workflow_id, name,
                     nodes_json, connections_json, std::move(callback)),
      headers);
}

void UpdateN8nWorkflowTool::OnCurrentWorkflowFetched(
    std::string workflow_id,
    std::string name,
    std::string nodes_json,
    std::string connections_json,
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()),
             " fetching the current workflow - check the workflow_id."})),
        {});
    return;
  }
  std::string current_json;
  base::JSONWriter::Write(result.value_body(), &current_json);
  manager_->SaveWorkflowVersionSnapshot(
      workflow_id, current_json,
      base::BindOnce(&UpdateN8nWorkflowTool::OnSnapshotSaved,
                     weak_ptr_factory_.GetWeakPtr(), workflow_id, name,
                     nodes_json, connections_json, std::move(callback)));
}

void UpdateN8nWorkflowTool::OnSnapshotSaved(std::string workflow_id,
                                            std::string name,
                                            std::string nodes_json,
                                            std::string connections_json,
                                            UseToolCallback callback,
                                            bool snapshot_success) {
  // Proceeds with the update either way - the snapshot is a best-effort
  // safety net (like PerformBackup()), not a precondition for the actual
  // change the user/AI asked for.
  auto nodes = base::JSONReader::Read(nodes_json,
                                      base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  auto connections = base::JSONReader::Read(
      connections_json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!nodes || !nodes->is_list() || !connections ||
      !connections->is_dict()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: 'nodes' must be a JSON array and 'connections' a JSON "
            "object."),
        {});
    return;
  }
  std::string body_json =
      BuildWorkflowBodyJson(name, std::move(*nodes), std::move(*connections));

  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key = prefs ? N8nProcessManager::GetApiKey(prefs) : "";
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "PUT",
      GURL(base::StrCat(
          {manager_->base_url(), "/api/v1/workflows/", workflow_id})),
      body_json, "application/json",
      base::BindOnce(&UpdateN8nWorkflowTool::OnUpdateResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void UpdateN8nWorkflowTool::OnUpdateResponse(
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  std::string body_text;
  base::JSONWriter::Write(result.value_body(), &body_text);
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()), ": ",
             body_text,
             " (the previous version was still saved to local history)."})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Updated workflow (previous version saved to history). ",
           body_text})),
      {});
}

// ListN8nWorkflowVersionsTool ---------------------------------------------

ListN8nWorkflowVersionsTool::ListN8nWorkflowVersionsTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager) {}

ListN8nWorkflowVersionsTool::~ListN8nWorkflowVersionsTool() = default;

std::string_view ListN8nWorkflowVersionsTool::Name() const {
  return "list_n8n_workflow_versions";
}

std::string_view ListN8nWorkflowVersionsTool::Description() const {
  return "Lists locally-saved version snapshots for one n8n workflow "
         "(saved automatically by update_n8n_workflow and "
         "rollback_n8n_workflow), oldest first. Use a returned timestamp "
         "with rollback_n8n_workflow to restore that version.";
}

std::optional<base::DictValue> ListN8nWorkflowVersionsTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowId, StringProperty("The workflow's id.")}});
}

std::optional<std::vector<std::string>>
ListN8nWorkflowVersionsTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameWorkflowId};
}

void ListN8nWorkflowVersionsTool::UseTool(const std::string& input_json,
                                          UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* workflow_id =
      input.has_value() ? input->FindString(kPropertyNameWorkflowId)
                        : nullptr;
  if (!workflow_id || workflow_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'workflow_id'"), {});
    return;
  }
  manager_->ListWorkflowVersions(
      *workflow_id,
      base::BindOnce(&ListN8nWorkflowVersionsTool::OnVersionsListed,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ListN8nWorkflowVersionsTool::OnVersionsListed(
    UseToolCallback callback,
    std::vector<std::string> timestamps) {
  if (timestamps.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "No saved versions for this workflow yet - versions are "
            "saved automatically the first time update_n8n_workflow or "
            "rollback_n8n_workflow is called on it."),
        {});
    return;
  }
  std::string text = "Saved versions (oldest first):\n";
  for (const auto& timestamp : timestamps) {
    base::StrAppend(&text, {"  - ", timestamp, "\n"});
  }
  std::move(callback).Run(CreateContentBlocksForText(text), {});
}

// RollbackN8nWorkflowTool -------------------------------------------------

RollbackN8nWorkflowTool::RollbackN8nWorkflowTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {
  auto url_loader_factory = browser_context_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetN8nTrafficAnnotationTag(), std::move(url_loader_factory));
}

RollbackN8nWorkflowTool::~RollbackN8nWorkflowTool() = default;

std::string_view RollbackN8nWorkflowTool::Name() const {
  return "rollback_n8n_workflow";
}

std::string_view RollbackN8nWorkflowTool::Description() const {
  return "Rolls an n8n workflow back to a previously-saved local version "
         "(see list_n8n_workflow_versions for available timestamps). The "
         "workflow's state right before the rollback is itself saved "
         "first, so this can always be undone. Starts n8n first if it "
         "isn't already running.";
}

std::optional<base::DictValue> RollbackN8nWorkflowTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowId, StringProperty("The workflow's id.")},
       {kPropertyNameVersionTimestamp,
        StringProperty("The version's timestamp, from "
                       "list_n8n_workflow_versions.")}});
}

std::optional<std::vector<std::string>>
RollbackN8nWorkflowTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameWorkflowId,
                                  kPropertyNameVersionTimestamp};
}

void RollbackN8nWorkflowTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* workflow_id =
      input.has_value() ? input->FindString(kPropertyNameWorkflowId)
                        : nullptr;
  const std::string* timestamp =
      input.has_value() ? input->FindString(kPropertyNameVersionTimestamp)
                        : nullptr;
  if (!workflow_id || workflow_id->empty() || !timestamp ||
      timestamp->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing 'workflow_id' or 'version_timestamp'"),
        {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &RollbackN8nWorkflowTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      *workflow_id, *timestamp, std::move(callback)));
}

void RollbackN8nWorkflowTool::OnStarted(std::string workflow_id,
                                        std::string timestamp,
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
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "GET",
      GURL(base::StrCat(
          {manager_->base_url(), "/api/v1/workflows/", workflow_id})),
      "", "",
      base::BindOnce(
          &RollbackN8nWorkflowTool::OnCurrentWorkflowFetchedForRollback,
          weak_ptr_factory_.GetWeakPtr(), workflow_id, timestamp,
          std::move(callback)),
      headers);
}

void RollbackN8nWorkflowTool::OnCurrentWorkflowFetchedForRollback(
    std::string workflow_id,
    std::string timestamp,
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()),
             " fetching the current workflow - check the workflow_id."})),
        {});
    return;
  }
  std::string current_json;
  base::JSONWriter::Write(result.value_body(), &current_json);
  manager_->SaveWorkflowVersionSnapshot(
      workflow_id, current_json,
      base::BindOnce(&RollbackN8nWorkflowTool::OnPreRollbackSnapshotSaved,
                     weak_ptr_factory_.GetWeakPtr(), workflow_id, timestamp,
                     std::move(callback)));
}

void RollbackN8nWorkflowTool::OnPreRollbackSnapshotSaved(
    std::string workflow_id,
    std::string timestamp,
    UseToolCallback callback,
    bool snapshot_success) {
  manager_->ReadWorkflowVersionSnapshot(
      workflow_id, timestamp,
      base::BindOnce(&RollbackN8nWorkflowTool::OnVersionRead,
                     weak_ptr_factory_.GetWeakPtr(), workflow_id,
                     std::move(callback)));
}

void RollbackN8nWorkflowTool::OnVersionRead(
    std::string workflow_id,
    UseToolCallback callback,
    std::optional<std::string> workflow_json) {
  if (!workflow_json) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: that version wasn't found. Call "
            "list_n8n_workflow_versions to see what's actually saved."),
        {});
    return;
  }
  auto parsed = base::JSONReader::ReadDict(*workflow_json,
                                           base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: the saved version's JSON is corrupted."),
        {});
    return;
  }
  const std::string* name = parsed->FindString("name");
  base::Value* nodes = parsed->Find("nodes");
  base::Value* connections = parsed->Find("connections");
  if (!name || !nodes || !connections) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: the saved version is missing 'name', 'nodes', or "
            "'connections'."),
        {});
    return;
  }
  std::string body_json =
      BuildWorkflowBodyJson(*name, std::move(*nodes), std::move(*connections));

  auto* prefs = user_prefs::UserPrefs::Get(browser_context_);
  std::string api_key = prefs ? N8nProcessManager::GetApiKey(prefs) : "";
  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-N8N-API-KEY", api_key);
  api_request_helper_->Request(
      "PUT",
      GURL(base::StrCat(
          {manager_->base_url(), "/api/v1/workflows/", workflow_id})),
      body_json, "application/json",
      base::BindOnce(&RollbackN8nWorkflowTool::OnRollbackResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

void RollbackN8nWorkflowTool::OnRollbackResponse(
    UseToolCallback callback,
    api_request_helper::APIRequestResult result) {
  std::string body_text;
  base::JSONWriter::Write(result.value_body(), &body_text);
  if (!result.Is2XXResponseCode()) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: n8n returned ",
             base::NumberToString(result.response_code()), ": ",
             body_text})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Rolled back workflow. ", body_text})),
      {});
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_TOOLS_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

class PrefService;

namespace content {
class BrowserContext;
}  // namespace content

namespace api_request_helper {
class APIRequestHelper;
class APIRequestResult;
}  // namespace api_request_helper

namespace ai_chat {

class N8nProcessManager;

// Starts the local n8n instance if it isn't already running, and opens its
// editor UI in a new tab. Use this when the user wants to build/edit a flow
// visually rather than have you generate its JSON definition directly.
class OpenN8nTool : public Tool {
 public:
  OpenN8nTool(N8nProcessManager* manager,
             content::BrowserContext* browser_context);
  ~OpenN8nTool() override;

  OpenN8nTool(const OpenN8nTool&) = delete;
  OpenN8nTool& operator=(const OpenN8nTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(UseToolCallback callback, bool success);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<OpenN8nTool> weak_ptr_factory_{this};
};

// Stores the n8n Public API key the user generated via n8n's own UI
// (Settings > n8n API, after completing its one-time owner-account setup -
// this can't be automated from outside, it's n8n's own login/account
// creation flow). create_n8n_workflow and run_n8n_workflow need this to
// call anything; without it they return a clear explanatory error instead
// of failing silently.
class SetN8nApiKeyTool : public Tool {
 public:
  explicit SetN8nApiKeyTool(content::BrowserContext* browser_context);
  ~SetN8nApiKeyTool() override;

  SetN8nApiKeyTool(const SetN8nApiKeyTool&) = delete;
  SetN8nApiKeyTool& operator=(const SetN8nApiKeyTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Creates a new n8n workflow from a JSON definition (n8n's own node/
// connection graph format - the same shape n8n's editor itself saves/loads,
// see https://docs.n8n.io/workflows/ for the node/connection schema) via
// its versioned Public REST API (requires an API key - see
// SetN8nApiKeyTool). Starts n8n first if it isn't already running.
class CreateN8nWorkflowTool : public Tool {
 public:
  CreateN8nWorkflowTool(N8nProcessManager* manager,
                        content::BrowserContext* browser_context);
  ~CreateN8nWorkflowTool() override;

  CreateN8nWorkflowTool(const CreateN8nWorkflowTool&) = delete;
  CreateN8nWorkflowTool& operator=(const CreateN8nWorkflowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(std::string name,
                std::string nodes_json,
                std::string connections_json,
                UseToolCallback callback,
                bool success);
  void OnCreateResponse(UseToolCallback callback,
                        api_request_helper::APIRequestResult result);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;

  base::WeakPtrFactory<CreateN8nWorkflowTool> weak_ptr_factory_{this};
};

// Activates an existing n8n workflow by id, via its versioned Public REST
// API (requires an API key - see SetN8nApiKeyTool). Starts n8n first if it
// isn't already running.
//
// Verified against a real running instance: n8n's Public API has no
// endpoint for triggering a one-off manual run of a workflow (only its
// internal, session-authenticated editor API does, via its "Execute"
// button) - activation is what's actually available, and it's the correct
// mechanism for workflows with a Schedule/Webhook/other event trigger,
// which then starts firing on its own going forward. A workflow with only
// a Manual Trigger node can't be activated (n8n rejects it) - those need
// to be run from n8n's own UI via open_n8n instead.
class RunN8nWorkflowTool : public Tool {
 public:
  RunN8nWorkflowTool(N8nProcessManager* manager,
                     content::BrowserContext* browser_context);
  ~RunN8nWorkflowTool() override;

  RunN8nWorkflowTool(const RunN8nWorkflowTool&) = delete;
  RunN8nWorkflowTool& operator=(const RunN8nWorkflowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(std::string workflow_id,
                UseToolCallback callback,
                bool success);
  void OnRunResponse(UseToolCallback callback,
                     api_request_helper::APIRequestResult result);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;

  base::WeakPtrFactory<RunN8nWorkflowTool> weak_ptr_factory_{this};
};

// Updates an existing n8n workflow's node/connection graph by id, via its
// versioned Public REST API. Before overwriting, automatically snapshots
// the workflow's *current* definition (fetched fresh from n8n) to local
// version history - see RollbackN8nWorkflowTool and
// N8nProcessManager::SaveWorkflowVersionSnapshot. Starts n8n first if it
// isn't already running.
class UpdateN8nWorkflowTool : public Tool {
 public:
  UpdateN8nWorkflowTool(N8nProcessManager* manager,
                        content::BrowserContext* browser_context);
  ~UpdateN8nWorkflowTool() override;

  UpdateN8nWorkflowTool(const UpdateN8nWorkflowTool&) = delete;
  UpdateN8nWorkflowTool& operator=(const UpdateN8nWorkflowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(std::string workflow_id,
                std::string name,
                std::string nodes_json,
                std::string connections_json,
                UseToolCallback callback,
                bool success);
  void OnCurrentWorkflowFetched(std::string workflow_id,
                                std::string name,
                                std::string nodes_json,
                                std::string connections_json,
                                UseToolCallback callback,
                                api_request_helper::APIRequestResult result);
  void OnSnapshotSaved(std::string workflow_id,
                       std::string name,
                       std::string nodes_json,
                       std::string connections_json,
                       UseToolCallback callback,
                       bool snapshot_success);
  void OnUpdateResponse(UseToolCallback callback,
                        api_request_helper::APIRequestResult result);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;

  base::WeakPtrFactory<UpdateN8nWorkflowTool> weak_ptr_factory_{this};
};

// Lists the locally-saved version snapshots for one workflow (see
// UpdateN8nWorkflowTool), most recent last. Use the returned timestamps
// with RollbackN8nWorkflowTool.
class ListN8nWorkflowVersionsTool : public Tool {
 public:
  ListN8nWorkflowVersionsTool(N8nProcessManager* manager,
                              content::BrowserContext* browser_context);
  ~ListN8nWorkflowVersionsTool() override;

  ListN8nWorkflowVersionsTool(const ListN8nWorkflowVersionsTool&) = delete;
  ListN8nWorkflowVersionsTool& operator=(const ListN8nWorkflowVersionsTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnVersionsListed(UseToolCallback callback,
                        std::vector<std::string> timestamps);

  raw_ptr<N8nProcessManager> manager_ = nullptr;

  base::WeakPtrFactory<ListN8nWorkflowVersionsTool> weak_ptr_factory_{this};
};

// Rolls a workflow back to a previously-saved local version snapshot (see
// ListN8nWorkflowVersionsTool for available timestamps), by PUTting that
// snapshot's JSON back to n8n. Snapshots the workflow's state immediately
// before the rollback too, so rolling back is itself reversible. Starts
// n8n first if it isn't already running.
class RollbackN8nWorkflowTool : public Tool {
 public:
  RollbackN8nWorkflowTool(N8nProcessManager* manager,
                          content::BrowserContext* browser_context);
  ~RollbackN8nWorkflowTool() override;

  RollbackN8nWorkflowTool(const RollbackN8nWorkflowTool&) = delete;
  RollbackN8nWorkflowTool& operator=(const RollbackN8nWorkflowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(std::string workflow_id,
                std::string timestamp,
                UseToolCallback callback,
                bool success);
  void OnCurrentWorkflowFetchedForRollback(
      std::string workflow_id,
      std::string timestamp,
      UseToolCallback callback,
      api_request_helper::APIRequestResult result);
  void OnPreRollbackSnapshotSaved(std::string workflow_id,
                                  std::string timestamp,
                                  UseToolCallback callback,
                                  bool snapshot_success);
  void OnVersionRead(std::string workflow_id,
                     UseToolCallback callback,
                     std::optional<std::string> workflow_json);
  void OnRollbackResponse(UseToolCallback callback,
                          api_request_helper::APIRequestResult result);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;

  base::WeakPtrFactory<RollbackN8nWorkflowTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_TOOLS_H_

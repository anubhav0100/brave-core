/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_SETTINGS_LEO_ASSISTANT_HANDLER_H_
#define BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_SETTINGS_LEO_ASSISTANT_HANDLER_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/ai_chat/workflows/workflow_runtime.h"
#include "brave/browser/colibri/colibri_process_manager.h"
#include "brave/browser/delegation/delegation_process_manager.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom-forward.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "brave/components/sidebar/browser/sidebar_service.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

class Profile;

namespace settings {

class BraveLeoAssistantHandler
    : public settings::SettingsPageUIHandler,
      public sidebar::SidebarService::Observer,
      public ai_chat::N8nProcessManager::Observer,
      public ai_chat::DelegationProcessManager::Observer,
      public ai_chat::ColibriProcessManager::Observer {
 public:
  BraveLeoAssistantHandler();
  ~BraveLeoAssistantHandler() override;

  BraveLeoAssistantHandler(const BraveLeoAssistantHandler&) = delete;
  BraveLeoAssistantHandler& operator=(const BraveLeoAssistantHandler&) = delete;

 private:
  // SettingsPageUIHandler overrides:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // sidebar::SidebarService::Observer overrides
  void OnItemAdded(const sidebar::SidebarItem& item, size_t index) override;
  void OnItemRemoved(const sidebar::SidebarItem& item, size_t index) override;

  // ai_chat::N8nProcessManager::Observer overrides
  void OnN8nOutputAppended(const std::string& text) override;
  void OnN8nRunningStateChanged(bool running) override;

  // ai_chat::DelegationProcessManager::Observer overrides
  void OnDelegationOutputAppended(const std::string& text) override;
  void OnDelegationRunningStateChanged(bool running) override;

  // ai_chat::ColibriProcessManager::Observer overrides
  void OnColibriOutputAppended(const std::string& text) override;
  void OnColibriRunningStateChanged(bool running) override;

  void NotifyChatUiChanged(const bool& isLeoVisible);

  void HandleValidateModelEndpoint(const base::ListValue& args);
  void HandleToggleLeoIcon(const base::ListValue& args);
  void HandleGetLeoIconVisibility(const base::ListValue& args);
  void HandleResetLeoData(const base::ListValue& args);
  void HandleFetchAvailableModels(const base::ListValue& args);
  void OnFetchAvailableModelsResponse(
      base::Value callback_id,
      api_request_helper::APIRequestResult result);
  void HandleGetPageCaptureData(const base::ListValue& args);
  void HandleGetWebhookTools(const base::ListValue& args);
  void HandleAddWebhookTool(const base::ListValue& args);
  void HandleUpdateWebhookTool(const base::ListValue& args);
  void HandleDeleteWebhookTool(const base::ListValue& args);
  void HandleGetAIChatConversations(const base::ListValue& args);
  void OnGetAIChatConversationsResponse(
      base::Value callback_id,
      std::vector<ai_chat::mojom::ConversationPtr> conversations);
  void HandleOpenAIChatConversation(const base::ListValue& args);
  void HandleGetWorkflows(const base::ListValue& args);
  void HandleSaveWorkflow(const base::ListValue& args);
  void HandlePublishWorkflow(const base::ListValue& args);
  void HandleDeleteWorkflow(const base::ListValue& args);
  void HandleRunWorkflow(const base::ListValue& args);
  void OnRunWorkflowComplete(base::Value callback_id,
                             ai_chat::WorkflowRuntime::ExecutionResult result);
  void HandleGetContentIndexStatus(const base::ListValue& args);
  void HandleClearContentIndex(const base::ListValue& args);
  void HandleGetN8nStatus(const base::ListValue& args);
  void HandleGetN8nBufferedOutput(const base::ListValue& args);
  void HandleStartN8n(const base::ListValue& args);
  void OnN8nStarted(base::Value callback_id, bool success);
  void HandleGetDelegationStatus(const base::ListValue& args);
  void HandleGetDelegationBufferedOutput(const base::ListValue& args);
  void HandleStartDelegation(const base::ListValue& args);
  void OnDelegationStarted(base::Value callback_id, bool success);
  void HandleGetColibriStatus(const base::ListValue& args);
  void HandleGetColibriBufferedOutput(const base::ListValue& args);
  void HandleStartColibri(const base::ListValue& args);
  void OnColibriStarted(base::Value callback_id, bool success);
  void HandleGetMcpWorkflows(const base::ListValue& args);
  void OnMcpWorkflowsListedForSettings(
      base::Value callback_id,
      bool success,
      std::string error_message,
      std::vector<ai_chat::N8nProcessManager::McpWorkflowInfo> workflows);
  void HandleSetMcpWorkflowEnabled(const base::ListValue& args);

  raw_ptr<Profile> profile_ = nullptr;
  base::ScopedObservation<sidebar::SidebarService,
                          sidebar::SidebarService::Observer>
      sidebar_service_observer_{this};
  base::ScopedObservation<ai_chat::N8nProcessManager,
                          ai_chat::N8nProcessManager::Observer>
      n8n_process_manager_observer_{this};
  base::ScopedObservation<ai_chat::DelegationProcessManager,
                          ai_chat::DelegationProcessManager::Observer>
      delegation_process_manager_observer_{this};
  base::ScopedObservation<ai_chat::ColibriProcessManager,
                          ai_chat::ColibriProcessManager::Observer>
      colibri_process_manager_observer_{this};
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;
  base::WeakPtrFactory<BraveLeoAssistantHandler> weak_ptr_factory_{this};
};

}  // namespace settings

#endif  // BRAVE_BROWSER_UI_WEBUI_SETTINGS_BRAVE_SETTINGS_LEO_ASSISTANT_HANDLER_H_

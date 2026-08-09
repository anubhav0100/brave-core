/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/settings/brave_settings_leo_assistant_handler.h"

#include <algorithm>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "brave/browser/ai_chat/ai_chat_service_factory.h"
#include "brave/browser/ai_chat/webhook_tool_service.h"
#include "brave/browser/ai_chat/webhook_tool_service_factory.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"
#include "brave/browser/ai_chat/workflows/workflow_repository.h"
#include "brave/browser/ai_chat/workflows/workflow_repository_factory.h"
#include "brave/browser/n8n/n8n_process_manager_factory.h"
#include "brave/browser/ui/sidebar/sidebar_service_factory.h"
#include "brave/browser/ui/views/side_panel/page_capture/page_capture_side_panel_coordinator.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "brave/components/ai_chat/core/browser/ai_chat_service.h"
#include "brave/components/ai_chat/core/browser/model_validator.h"
#include "brave/components/ai_chat/core/browser/utils.h"
#include "brave/components/ai_chat/core/common/ai_chat_urls.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/prefs.h"
#include "brave/components/sidebar/browser/sidebar_item.h"
#include "brave/components/sidebar/browser/sidebar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/base/page_transition_types.h"

namespace {

const std::vector<sidebar::SidebarItem>::const_iterator FindAiChatSidebarItem(
    const std::vector<sidebar::SidebarItem>& items) {
  return std::ranges::find_if(items, [](const auto& item) {
    return item.built_in_item_type ==
           sidebar::SidebarItem::BuiltInItemType::kChatUI;
  });
}

bool ShowLeoAssistantIconVisibleIfNot(
    sidebar::SidebarService* sidebar_service) {
  const auto hidden_items = sidebar_service->GetHiddenDefaultSidebarItems();
  const auto item_hidden_iter = FindAiChatSidebarItem(hidden_items);

  if (item_hidden_iter != hidden_items.end()) {
    sidebar_service->AddItem(*item_hidden_iter);
    return true;
  }

  return false;
}

bool HideLeoAssistantIconIfNot(sidebar::SidebarService* sidebar_service) {
  const auto visible_items = sidebar_service->items();
  const auto item_visible_iter = FindAiChatSidebarItem(visible_items);

  if (item_visible_iter != visible_items.end()) {
    sidebar_service->RemoveItemAt(item_visible_iter - visible_items.begin());
    return true;
  }

  return false;
}

ai_chat::WebhookToolConfig WebhookToolConfigFromDict(
    const base::DictValue& dict) {
  ai_chat::WebhookToolConfig config;
  if (const std::string* name = dict.FindString("name")) {
    config.name = *name;
  }
  if (const std::string* description = dict.FindString("description")) {
    config.description = *description;
  }
  if (const std::string* url = dict.FindString("url")) {
    config.url = *url;
  }
  if (const std::string* secret = dict.FindString("secret")) {
    config.secret = *secret;
  }
  config.enabled = dict.FindBool("enabled").value_or(true);
  if (const base::ListValue* params = dict.FindList("parameters")) {
    for (const auto& param_value : *params) {
      if (!param_value.is_dict()) {
        continue;
      }
      const auto& param_dict = param_value.GetDict();
      ai_chat::WebhookToolConfig::Parameter param;
      if (const std::string* name = param_dict.FindString("name")) {
        param.name = *name;
      }
      if (const std::string* description =
              param_dict.FindString("description")) {
        param.description = *description;
      }
      param.required = param_dict.FindBool("required").value_or(false);
      config.parameters.push_back(std::move(param));
    }
  }
  return config;
}

base::DictValue WebhookToolConfigToDict(
    const ai_chat::WebhookToolConfig& config) {
  base::DictValue dict;
  dict.Set("id", config.id);
  dict.Set("name", config.name);
  dict.Set("description", config.description);
  dict.Set("url", config.url);
  dict.Set("enabled", config.enabled);
  dict.Set("hasSecret", !config.secret.empty());
  base::ListValue params;
  for (const auto& param : config.parameters) {
    base::DictValue param_dict;
    param_dict.Set("name", param.name);
    param_dict.Set("description", param.description);
    param_dict.Set("required", param.required);
    params.Append(std::move(param_dict));
  }
  dict.Set("parameters", std::move(params));
  return dict;
}

}  // namespace

namespace settings {

BraveLeoAssistantHandler::BraveLeoAssistantHandler() = default;

BraveLeoAssistantHandler::~BraveLeoAssistantHandler() = default;

void BraveLeoAssistantHandler::RegisterMessages() {
  profile_ = Profile::FromWebUI(web_ui());

  web_ui()->RegisterMessageCallback(
      "toggleLeoIcon",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleToggleLeoIcon,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getLeoIconVisibility",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleGetLeoIconVisibility,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resetLeoData",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleResetLeoData,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "validateModelEndpoint",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleValidateModelEndpoint,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fetchAvailableModels",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleFetchAvailableModels,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPageCaptureData",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleGetPageCaptureData,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getWebhookTools",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleGetWebhookTools,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "addWebhookTool",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleAddWebhookTool,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "updateWebhookTool",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleUpdateWebhookTool,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteWebhookTool",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleDeleteWebhookTool,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getAIChatConversations",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleGetAIChatConversations,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openAIChatConversation",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleOpenAIChatConversation,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getWorkflows",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleGetWorkflows,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveWorkflow",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleSaveWorkflow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "publishWorkflow",
      base::BindRepeating(&BraveLeoAssistantHandler::HandlePublishWorkflow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteWorkflow",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleDeleteWorkflow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "runWorkflow",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleRunWorkflow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getContentIndexStatus",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleGetContentIndexStatus,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearContentIndex",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleClearContentIndex,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getN8nStatus",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleGetN8nStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getN8nBufferedOutput",
      base::BindRepeating(
          &BraveLeoAssistantHandler::HandleGetN8nBufferedOutput,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startN8n",
      base::BindRepeating(&BraveLeoAssistantHandler::HandleStartN8n,
                          base::Unretained(this)));
}

void BraveLeoAssistantHandler::OnJavascriptAllowed() {
  sidebar_service_observer_.Reset();
  sidebar_service_observer_.Observe(
      sidebar::SidebarServiceFactory::GetForProfile(profile_));

  n8n_process_manager_observer_.Reset();
  if (auto* n8n_manager =
          ai_chat::N8nProcessManagerFactory::GetForBrowserContext(profile_)) {
    n8n_process_manager_observer_.Observe(n8n_manager);
  }
}

void BraveLeoAssistantHandler::OnJavascriptDisallowed() {
  sidebar_service_observer_.Reset();
  n8n_process_manager_observer_.Reset();
}

void BraveLeoAssistantHandler::OnN8nOutputAppended(const std::string& text) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("n8n-output-appended", base::Value(text));
}

void BraveLeoAssistantHandler::OnN8nRunningStateChanged(bool running) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("n8n-running-state-changed", base::Value(running));
}

void BraveLeoAssistantHandler::OnItemAdded(const sidebar::SidebarItem& item,
                                           size_t index) {
  if (item.built_in_item_type ==
      sidebar::SidebarItem::BuiltInItemType::kChatUI) {
    NotifyChatUiChanged(true);
  }
}

void BraveLeoAssistantHandler::OnItemRemoved(const sidebar::SidebarItem& item,
                                             size_t index) {
  if (item.built_in_item_type ==
      sidebar::SidebarItem::BuiltInItemType::kChatUI) {
    NotifyChatUiChanged(false);
  }
}

void BraveLeoAssistantHandler::NotifyChatUiChanged(const bool& is_leo_visible) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("settings-brave-leo-assistant-changed", is_leo_visible);
}

void BraveLeoAssistantHandler::HandleToggleLeoIcon(
    const base::ListValue& args) {
  auto* service = sidebar::SidebarServiceFactory::GetForProfile(profile_);

  AllowJavascript();
  if (!ShowLeoAssistantIconVisibleIfNot(service)) {
    HideLeoAssistantIconIfNot(service);
  }
}

void BraveLeoAssistantHandler::HandleValidateModelEndpoint(
    const base::ListValue& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[1].is_dict()) {
    // Expect the appropriate number and type of arguments, or reject
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }

  const base::DictValue& dict = args[1].GetDict();
  GURL endpoint(*dict.FindString("url"));

  base::DictValue response;

  const bool is_valid = ai_chat::ModelValidator::IsValidEndpoint(endpoint);

  response.Set("isValid", is_valid);
  response.Set("isValidAsPrivateEndpoint",
               ai_chat::ModelValidator::IsValidEndpoint(
                   endpoint, std::optional<bool>(true)));
  response.Set("isValidDueToPrivateIPsFeature",
               is_valid && ai_chat::features::IsAllowPrivateIPsEnabled() &&
                   !ai_chat::ModelValidator::IsValidEndpoint(
                       endpoint, std::optional<bool>(false)));

  ResolveJavascriptCallback(args[0], response);
}

void BraveLeoAssistantHandler::HandleGetLeoIconVisibility(
    const base::ListValue& args) {
  auto* service = sidebar::SidebarServiceFactory::GetForProfile(profile_);
  const auto hidden_items = service->GetHiddenDefaultSidebarItems();
  AllowJavascript();
  ResolveJavascriptCallback(
      args[0], !std::ranges::contains(
                   hidden_items, sidebar::SidebarItem::BuiltInItemType::kChatUI,
                   &sidebar::SidebarItem::built_in_item_type));
}

void BraveLeoAssistantHandler::HandleGetPageCaptureData(
    const base::ListValue& args) {
  AllowJavascript();

  base::ListValue entries;
  base::ListValue log;

  // The coordinator lives per-browser-window, not per-profile, so this
  // surfaces whichever window most recently had focus for this profile -
  // the same one the user has actually been using Page Capture in.
  if (BrowserWindowInterface* browser =
          ProfileBrowserCollection::GetForProfile(profile_)
              ->FindTabbedBrowser()) {
    if (auto* coordinator =
            browser->GetFeatures().page_capture_side_panel_coordinator()) {
      for (const auto& entry : coordinator->session_entries()) {
        base::DictValue entry_dict;
        entry_dict.Set("heading", entry.heading);
        constexpr size_t kPreviewChars = 300;
        entry_dict.Set("preview",
                       entry.refined_content.size() > kPreviewChars
                           ? entry.refined_content.substr(0, kPreviewChars) +
                                 "..."
                           : entry.refined_content);
        entries.Append(std::move(entry_dict));
      }
      for (const auto& log_entry : coordinator->activity_log()) {
        base::DictValue log_dict;
        log_dict.Set("timestampMs",
                     static_cast<double>(
                         log_entry.timestamp.InMillisecondsSinceUnixEpoch()));
        log_dict.Set("message", log_entry.message);
        log.Append(std::move(log_dict));
      }
    }
  }

  base::DictValue result;
  result.Set("entries", std::move(entries));
  result.Set("log", std::move(log));
  ResolveJavascriptCallback(args[0], result);
}

void BraveLeoAssistantHandler::HandleGetWebhookTools(
    const base::ListValue& args) {
  AllowJavascript();
  base::ListValue tools;
  if (auto* service =
          ai_chat::WebhookToolServiceFactory::GetForBrowserContext(
              profile_)) {
    for (const auto& config : service->GetTools()) {
      tools.Append(WebhookToolConfigToDict(config));
    }
  }
  ResolveJavascriptCallback(args[0], tools);
}

void BraveLeoAssistantHandler::HandleAddWebhookTool(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_dict()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* service =
      ai_chat::WebhookToolServiceFactory::GetForBrowserContext(profile_);
  if (!service) {
    RejectJavascriptCallback(
        args[0], base::Value("AI Chat is not available in this profile."));
    return;
  }
  ai_chat::WebhookToolConfig config =
      WebhookToolConfigFromDict(args[1].GetDict());
  std::string id = service->AddTool(std::move(config));
  ResolveJavascriptCallback(args[0], base::Value(id));
}

void BraveLeoAssistantHandler::HandleUpdateWebhookTool(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_dict()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  const base::DictValue& dict = args[1].GetDict();
  const std::string* id = dict.FindString("id");
  if (!id || id->empty()) {
    RejectJavascriptCallback(args[0], base::Value("Missing id"));
    return;
  }
  auto* service =
      ai_chat::WebhookToolServiceFactory::GetForBrowserContext(profile_);
  if (!service) {
    RejectJavascriptCallback(
        args[0], base::Value("AI Chat is not available in this profile."));
    return;
  }
  ai_chat::WebhookToolConfig config = WebhookToolConfigFromDict(dict);
  if (config.secret.empty()) {
    // Blank means "leave the existing secret unchanged" - the field isn't
    // re-populated with the real secret when editing, so an empty value
    // here is ambiguous with the user genuinely wanting no secret, and we
    // favor not silently breaking auth on every edit.
    for (const auto& existing : service->GetTools()) {
      if (existing.id == *id) {
        config.secret = existing.secret;
        break;
      }
    }
  }
  bool success = service->UpdateTool(*id, config);
  ResolveJavascriptCallback(args[0], base::Value(success));
}

void BraveLeoAssistantHandler::HandleDeleteWebhookTool(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* service =
      ai_chat::WebhookToolServiceFactory::GetForBrowserContext(profile_);
  bool success = service && service->DeleteTool(args[1].GetString());
  ResolveJavascriptCallback(args[0], base::Value(success));
}

void BraveLeoAssistantHandler::HandleGetAIChatConversations(
    const base::ListValue& args) {
  AllowJavascript();
  auto* service =
      ai_chat::AIChatServiceFactory::GetForBrowserContext(profile_);
  if (!service) {
    ResolveJavascriptCallback(args[0], base::ListValue());
    return;
  }
  service->GetConversations(base::BindOnce(
      &BraveLeoAssistantHandler::OnGetAIChatConversationsResponse,
      weak_ptr_factory_.GetWeakPtr(), args[0].Clone()));
}

void BraveLeoAssistantHandler::OnGetAIChatConversationsResponse(
    base::Value callback_id,
    std::vector<ai_chat::mojom::ConversationPtr> conversations) {
  base::ListValue result;
  for (const auto& conversation : conversations) {
    // Temporary conversations aren't persisted and have nothing to reopen;
    // conversations with no content yet are just-created placeholders.
    if (conversation->temporary || !conversation->has_content) {
      continue;
    }
    base::DictValue dict;
    dict.Set("uuid", conversation->uuid);
    dict.Set("title", conversation->title);
    dict.Set("updatedTimeMs",
             static_cast<double>(
                 conversation->updated_time.InMillisecondsSinceUnixEpoch()));
    result.Append(std::move(dict));
  }
  ResolveJavascriptCallback(callback_id, result);
}

void BraveLeoAssistantHandler::HandleOpenAIChatConversation(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  NavigateParams params(profile_,
                        ai_chat::ConversationUrl(args[1].GetString()),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.referrer = content::Referrer();
  Navigate(&params);
  ResolveJavascriptCallback(args[0], base::Value(true));
}

void BraveLeoAssistantHandler::HandleGetWorkflows(
    const base::ListValue& args) {
  AllowJavascript();
  base::ListValue result;
  if (auto* repository =
          ai_chat::WorkflowRepositoryFactory::GetForBrowserContext(profile_)) {
    for (const auto& workflow : repository->ListWorkflows()) {
      base::DictValue dict;
      dict.Set("id", workflow.id);
      dict.Set("name", workflow.name);
      dict.Set("version", workflow.version);
      dict.Set("status", ai_chat::WorkflowStatusToString(workflow.status));
      std::string definition_json;
      base::JSONWriter::WriteWithOptions(
          workflow.ToValue(), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &definition_json);
      dict.Set("definitionJson", definition_json);
      result.Append(std::move(dict));
    }
  }
  ResolveJavascriptCallback(args[0], result);
}

void BraveLeoAssistantHandler::HandleSaveWorkflow(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* repository =
      ai_chat::WorkflowRepositoryFactory::GetForBrowserContext(profile_);
  if (!repository) {
    RejectJavascriptCallback(
        args[0], base::Value("Workflows are not available in this profile."));
    return;
  }

  auto parsed = base::JSONReader::ReadDict(args[1].GetString(),
                                           base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  base::DictValue response;
  if (!parsed.has_value()) {
    base::ListValue errors;
    base::DictValue error;
    error.Set("stepId", "");
    error.Set("message", "Could not parse as JSON.");
    errors.Append(std::move(error));
    response.Set("errors", std::move(errors));
    ResolveJavascriptCallback(args[0], response);
    return;
  }

  ai_chat::WorkflowRepository::SaveResult save_result =
      repository->SaveWorkflow(*parsed);
  base::ListValue errors;
  for (const auto& error : save_result.errors) {
    base::DictValue error_dict;
    error_dict.Set("stepId", error.step_id);
    error_dict.Set("message", error.message);
    errors.Append(std::move(error_dict));
  }
  response.Set("errors", std::move(errors));
  if (save_result.id) {
    response.Set("id", *save_result.id);
  }
  ResolveJavascriptCallback(args[0], response);
}

void BraveLeoAssistantHandler::HandlePublishWorkflow(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* repository =
      ai_chat::WorkflowRepositoryFactory::GetForBrowserContext(profile_);
  bool success =
      repository && repository->PublishWorkflow(args[1].GetString());
  ResolveJavascriptCallback(args[0], base::Value(success));
}

void BraveLeoAssistantHandler::HandleDeleteWorkflow(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* repository =
      ai_chat::WorkflowRepositoryFactory::GetForBrowserContext(profile_);
  bool success = repository && repository->DeleteWorkflow(args[1].GetString());
  ResolveJavascriptCallback(args[0], base::Value(success));
}

void BraveLeoAssistantHandler::HandleRunWorkflow(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }
  auto* repository =
      ai_chat::WorkflowRepositoryFactory::GetForBrowserContext(profile_);
  std::optional<ai_chat::WorkflowDefinition> definition =
      repository ? repository->GetWorkflow(args[1].GetString()) : std::nullopt;
  if (!definition) {
    RejectJavascriptCallback(args[0], base::Value("Workflow not found."));
    return;
  }

  content::WebContents* web_contents = nullptr;
  if (BrowserWindowInterface* browser =
          ProfileBrowserCollection::GetForProfile(profile_)
              ->FindTabbedBrowser()) {
    if (auto* tab = browser->GetActiveTabInterface()) {
      web_contents = tab->GetContents();
    }
  }
  if (!web_contents) {
    RejectJavascriptCallback(
        args[0], base::Value("No active tab to run the workflow against."));
    return;
  }

  ai_chat::WorkflowRuntime::Start(
      std::move(*definition), base::DictValue(), web_contents,
      base::BindOnce(&BraveLeoAssistantHandler::OnRunWorkflowComplete,
                     weak_ptr_factory_.GetWeakPtr(), args[0].Clone()));
}

void BraveLeoAssistantHandler::OnRunWorkflowComplete(
    base::Value callback_id,
    ai_chat::WorkflowRuntime::ExecutionResult result) {
  base::DictValue response;
  response.Set("success", result.success);
  response.Set("errorMessage", result.error_message);
  base::ListValue steps;
  for (const auto& step_id : result.executed_step_ids) {
    steps.Append(step_id);
  }
  response.Set("executedStepIds", std::move(steps));
  base::DictValue outputs;
  for (const auto& [name, value] : result.outputs) {
    outputs.Set(name, value);
  }
  response.Set("outputs", std::move(outputs));
  ResolveJavascriptCallback(callback_id, response);
}

void BraveLeoAssistantHandler::HandleGetContentIndexStatus(
    const base::ListValue& args) {
  AllowJavascript();
  base::DictValue result;
  auto* index =
      ai_chat::AiChatContentIndexFactory::GetForBrowserContext(profile_);
  result.Set("entryCount",
            static_cast<int>(index ? index->entry_count() : 0));
  result.Set("available", index && index->IsAvailable());
  result.Set("enabled",
            profile_ && ai_chat::AiChatContentIndex::IsEnabledForProfile(
                            profile_->GetPrefs()));
  ResolveJavascriptCallback(args[0], result);
}

void BraveLeoAssistantHandler::HandleClearContentIndex(
    const base::ListValue& args) {
  AllowJavascript();
  if (auto* index =
          ai_chat::AiChatContentIndexFactory::GetForBrowserContext(profile_)) {
    index->Clear();
  }
  ResolveJavascriptCallback(args[0], base::Value(true));
}

void BraveLeoAssistantHandler::HandleGetN8nStatus(
    const base::ListValue& args) {
  AllowJavascript();
  base::DictValue result;
  auto* n8n_manager =
      ai_chat::N8nProcessManagerFactory::GetForBrowserContext(profile_);
  result.Set("running", n8n_manager && n8n_manager->IsReady());
  result.Set("baseUrl", n8n_manager ? n8n_manager->base_url() : "");
  ResolveJavascriptCallback(args[0], result);
}

void BraveLeoAssistantHandler::HandleGetN8nBufferedOutput(
    const base::ListValue& args) {
  AllowJavascript();
  auto* n8n_manager =
      ai_chat::N8nProcessManagerFactory::GetForBrowserContext(profile_);
  ResolveJavascriptCallback(
      args[0], base::Value(n8n_manager ? n8n_manager->GetBufferedOutput()
                                       : std::string()));
}

void BraveLeoAssistantHandler::HandleStartN8n(const base::ListValue& args) {
  AllowJavascript();
  base::Value callback_id = args[0].Clone();
  auto* n8n_manager =
      ai_chat::N8nProcessManagerFactory::GetForBrowserContext(profile_);
  if (!n8n_manager) {
    ResolveJavascriptCallback(callback_id, base::Value(false));
    return;
  }
  n8n_manager->EnsureStarted(
      base::BindOnce(&BraveLeoAssistantHandler::OnN8nStarted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback_id)));
}

void BraveLeoAssistantHandler::OnN8nStarted(base::Value callback_id,
                                            bool success) {
  ResolveJavascriptCallback(callback_id, base::Value(success));
}

void BraveLeoAssistantHandler::HandleResetLeoData(const base::ListValue& args) {
  auto* sidebar_service =
      sidebar::SidebarServiceFactory::GetForProfile(profile_);

  ShowLeoAssistantIconVisibleIfNot(sidebar_service);

  ai_chat::AIChatService* service =
      ai_chat::AIChatServiceFactory::GetForBrowserContext(profile_);
  if (!service) {
    return;
  }
  service->DeleteConversations();
  if (profile_) {
    ai_chat::SetUserOptedIn(profile_->GetPrefs(), false);
    ai_chat::prefs::DeleteAllMemoriesFromPrefs(*profile_->GetPrefs());
    ai_chat::prefs::ResetCustomizationsPref(*profile_->GetPrefs());
  }

  AllowJavascript();
}

void BraveLeoAssistantHandler::HandleFetchAvailableModels(
    const base::ListValue& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[1].is_dict()) {
    RejectJavascriptCallback(args[0], base::Value("Invalid arguments"));
    return;
  }

  const base::DictValue& dict = args[1].GetDict();
  const std::string* endpoint = dict.FindString("endpoint");
  if (!endpoint || endpoint->empty()) {
    RejectJavascriptCallback(args[0], base::Value("Missing endpoint"));
    return;
  }
  const std::string* api_key = dict.FindString("apiKey");

  // Every provider this is meant for (OpenAI, Anthropic, Gemini, DeepSeek,
  // GLM, Kimi, Ollama, ...) exposes its catalog at "<base>/models" in the
  // OpenAI-compatible list-models shape: {"data": [{"id": "..."}, ...]}.
  // The "Server endpoint" field is documented to hold the full completions
  // URL (e.g. ".../v1/chat/completions"), so strip that suffix before
  // appending "/models" rather than producing ".../chat/completions/models".
  std::string endpoint_base = *endpoint;
  while (endpoint_base.size() > 1 && endpoint_base.back() == '/') {
    endpoint_base.pop_back();
  }
  static constexpr char kChatCompletionsSuffix[] = "/chat/completions";
  if (base::EndsWith(endpoint_base, kChatCompletionsSuffix,
                      base::CompareCase::INSENSITIVE_ASCII)) {
    endpoint_base.erase(endpoint_base.size() -
                         std::string_view(kChatCompletionsSuffix).size());
  }
  GURL models_url(base::StrCat({endpoint_base, "/models"}));
  if (!models_url.is_valid() ||
      !ai_chat::ModelValidator::IsValidEndpoint(models_url)) {
    RejectJavascriptCallback(args[0], base::Value("Invalid endpoint"));
    return;
  }

  if (!api_request_helper_) {
    static const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
        net::DefineNetworkTrafficAnnotation("leo_byom_fetch_models", R"(
          semantics {
            sender: "Brave Leo Bring-Your-Own-Model Settings"
            description:
              "Fetches the list of available models from a user-configured "
              "custom model endpoint, so the user can pick one instead of "
              "typing an exact model id by hand. Only sent when the user "
              "opens the 'Add Model' settings form and to the endpoint the "
              "user themselves entered."
            trigger:
              "User enters a custom model endpoint and API key in Leo's "
              "Bring-Your-Own-Model settings."
            data: "The user's own API key, sent as a Bearer token."
            destination: OTHER
            destination_other:
              "The user-specified third-party model endpoint."
            internal {
              contacts {
                email: "ai-chat@brave.com"
              }
            }
            user_data {
              type: NONE
            }
            last_reviewed: "2026-08-05"
          }
          policy {
            cookies_allowed: NO
            setting: "This feature cannot be disabled independently of Leo."
            policy_exception_justification:
              "Only sent to an endpoint the user themselves configured."
          })");
    api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
        kTrafficAnnotation, profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess());
  }

  base::flat_map<std::string, std::string> headers;
  if (api_key && !api_key->empty()) {
    headers["Authorization"] = base::StrCat({"Bearer ", *api_key});
  }

  api_request_helper_->Request(
      "GET", models_url, "", "",
      base::BindOnce(&BraveLeoAssistantHandler::OnFetchAvailableModelsResponse,
                     weak_ptr_factory_.GetWeakPtr(), args[0].Clone()),
      headers);
}

void BraveLeoAssistantHandler::OnFetchAvailableModelsResponse(
    base::Value callback_id,
    api_request_helper::APIRequestResult result) {
  if (!result.Is2XXResponseCode()) {
    RejectJavascriptCallback(
        callback_id, base::Value(base::StrCat(
                         {"Request failed with status ",
                          base::NumberToString(result.response_code())})));
    return;
  }

  base::ListValue model_ids;
  if (result.value_body().is_dict()) {
    const base::ListValue* data =
        result.value_body().GetDict().FindList("data");
    if (data) {
      for (const auto& entry : *data) {
        if (entry.is_dict()) {
          const std::string* id = entry.GetDict().FindString("id");
          if (id) {
            model_ids.Append(*id);
          }
        }
      }
    }
  }

  ResolveJavascriptCallback(callback_id, model_ids);
}

}  // namespace settings

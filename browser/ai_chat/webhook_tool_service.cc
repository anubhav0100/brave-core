// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/webhook_tool_service.h"

#include <algorithm>
#include <utility>

#include "base/strings/strcat.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace ai_chat {

namespace {

constexpr char kWebhookToolsPref[] = "brave.ai_chat.webhook_tools";

constexpr char kIdKey[] = "id";
constexpr char kNameKey[] = "name";
constexpr char kDescriptionKey[] = "description";
constexpr char kUrlKey[] = "url";
constexpr char kSecretKey[] = "secret";
constexpr char kEnabledKey[] = "enabled";
constexpr char kParametersKey[] = "parameters";
constexpr char kParamRequiredKey[] = "required";

base::DictValue ConfigToDict(const WebhookToolConfig& config) {
  base::DictValue dict;
  dict.Set(kIdKey, config.id);
  dict.Set(kNameKey, config.name);
  dict.Set(kDescriptionKey, config.description);
  dict.Set(kUrlKey, config.url);
  dict.Set(kSecretKey, config.secret);
  dict.Set(kEnabledKey, config.enabled);

  base::ListValue params;
  for (const auto& param : config.parameters) {
    base::DictValue param_dict;
    param_dict.Set(kNameKey, param.name);
    param_dict.Set(kDescriptionKey, param.description);
    param_dict.Set(kParamRequiredKey, param.required);
    params.Append(std::move(param_dict));
  }
  dict.Set(kParametersKey, std::move(params));
  return dict;
}

WebhookToolConfig DictToConfig(const base::DictValue& dict) {
  WebhookToolConfig config;
  if (const std::string* id = dict.FindString(kIdKey)) {
    config.id = *id;
  }
  if (const std::string* name = dict.FindString(kNameKey)) {
    config.name = *name;
  }
  if (const std::string* description = dict.FindString(kDescriptionKey)) {
    config.description = *description;
  }
  if (const std::string* url = dict.FindString(kUrlKey)) {
    config.url = *url;
  }
  if (const std::string* secret = dict.FindString(kSecretKey)) {
    config.secret = *secret;
  }
  config.enabled = dict.FindBool(kEnabledKey).value_or(true);

  if (const base::ListValue* params = dict.FindList(kParametersKey)) {
    for (const auto& param_value : *params) {
      if (!param_value.is_dict()) {
        continue;
      }
      const auto& param_dict = param_value.GetDict();
      WebhookToolConfig::Parameter param;
      if (const std::string* name = param_dict.FindString(kNameKey)) {
        param.name = *name;
      }
      if (const std::string* description =
              param_dict.FindString(kDescriptionKey)) {
        param.description = *description;
      }
      param.required = param_dict.FindBool(kParamRequiredKey).value_or(false);
      config.parameters.push_back(std::move(param));
    }
  }
  return config;
}

}  // namespace

WebhookToolConfig::WebhookToolConfig() = default;
WebhookToolConfig::WebhookToolConfig(const WebhookToolConfig&) = default;
WebhookToolConfig& WebhookToolConfig::operator=(const WebhookToolConfig&) =
    default;
WebhookToolConfig::~WebhookToolConfig() = default;

WebhookToolService::WebhookToolService(PrefService* prefs) : prefs_(prefs) {}

WebhookToolService::~WebhookToolService() = default;

// static
void WebhookToolService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kWebhookToolsPref);
}

std::vector<WebhookToolConfig> WebhookToolService::GetTools() const {
  std::vector<WebhookToolConfig> tools;
  for (const auto& item : prefs_->GetList(kWebhookToolsPref)) {
    if (item.is_dict()) {
      tools.push_back(DictToConfig(item.GetDict()));
    }
  }
  return tools;
}

std::string WebhookToolService::AddTool(WebhookToolConfig config) {
  config.id = base::StrCat(
      {"webhook:",
       base::Uuid::GenerateRandomV4().AsLowercaseString().substr(0, 8)});
  {
    ScopedListPrefUpdate update(prefs_, kWebhookToolsPref);
    update->Append(ConfigToDict(config));
  }
  for (auto& observer : observers_) {
    observer.OnWebhookToolsChanged();
  }
  return config.id;
}

bool WebhookToolService::UpdateTool(const std::string& id,
                                    WebhookToolConfig config) {
  bool found = false;
  {
    ScopedListPrefUpdate update(prefs_, kWebhookToolsPref);
    for (auto& item : *update) {
      if (item.is_dict()) {
        const std::string* item_id = item.GetDict().FindString(kIdKey);
        if (item_id && *item_id == id) {
          config.id = id;
          item = base::Value(ConfigToDict(config));
          found = true;
          break;
        }
      }
    }
  }
  if (found) {
    for (auto& observer : observers_) {
      observer.OnWebhookToolsChanged();
    }
  }
  return found;
}

bool WebhookToolService::DeleteTool(const std::string& id) {
  bool found = false;
  {
    ScopedListPrefUpdate update(prefs_, kWebhookToolsPref);
    auto it = std::ranges::find_if(*update, [&id](const base::Value& item) {
      const std::string* item_id =
          item.is_dict() ? item.GetDict().FindString(kIdKey) : nullptr;
      return item_id && *item_id == id;
    });
    if (it != update->end()) {
      update->erase(it);
      found = true;
    }
  }
  if (found) {
    for (auto& observer : observers_) {
      observer.OnWebhookToolsChanged();
    }
  }
  return found;
}

void WebhookToolService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void WebhookToolService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace ai_chat

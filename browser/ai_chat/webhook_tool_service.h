// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_SERVICE_H_
#define BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_SERVICE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefService;
class PrefRegistrySimple;

namespace ai_chat {

// One user-configured webhook tool: Leo calls it mid-conversation as a real
// function-calling Tool, POSTing the model's chosen arguments to `url` and
// feeding the response back as the tool's result.
struct WebhookToolConfig {
  struct Parameter {
    std::string name;
    std::string description;
    bool required = false;
  };

  WebhookToolConfig();
  WebhookToolConfig(const WebhookToolConfig&);
  WebhookToolConfig& operator=(const WebhookToolConfig&);
  ~WebhookToolConfig();

  std::string id;
  std::string name;
  std::string description;
  std::string url;
  // Sent as "Authorization: Bearer <secret>" when non-empty.
  std::string secret;
  bool enabled = true;
  std::vector<Parameter> parameters;
};

// Profile-scoped store of the user's configured webhook tools, persisted to
// prefs. Backs both the Settings UI (add/update/delete) and
// WebhookToolProvider, which turns the enabled configs into real Tool
// instances for each new conversation.
class WebhookToolService : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnWebhookToolsChanged() {}
  };

  explicit WebhookToolService(PrefService* prefs);
  ~WebhookToolService() override;

  WebhookToolService(const WebhookToolService&) = delete;
  WebhookToolService& operator=(const WebhookToolService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<WebhookToolConfig> GetTools() const;
  // Returns the new tool's generated id.
  std::string AddTool(WebhookToolConfig config);
  bool UpdateTool(const std::string& id, WebhookToolConfig config);
  bool DeleteTool(const std::string& id);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  raw_ptr<PrefService> prefs_;
  base::ObserverList<Observer> observers_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_SERVICE_H_

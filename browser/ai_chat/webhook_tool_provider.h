// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_H_
#define BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_H_

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/browser/tools/tool_provider.h"

class Profile;

namespace ai_chat {

class WebhookTool;
class WebhookToolService;

// Provides one real Tool per enabled user-configured webhook tool (Settings
// -> AI Assistant -> Custom tools), snapshotted at conversation start - an
// edit made in Settings mid-conversation applies starting with the next new
// conversation.
class WebhookToolProvider : public ToolProvider {
 public:
  WebhookToolProvider(Profile* profile, WebhookToolService* service);
  ~WebhookToolProvider() override;

  WebhookToolProvider(const WebhookToolProvider&) = delete;
  WebhookToolProvider& operator=(const WebhookToolProvider&) = delete;

  // ToolProvider:
  std::vector<base::WeakPtr<Tool>> GetTools() override;

 private:
  std::vector<std::unique_ptr<WebhookTool>> tools_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_H_

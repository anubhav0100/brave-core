// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_FACTORY_H_
#define BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_FACTORY_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool_provider_factory.h"

class Profile;

namespace ai_chat {

class WebhookToolService;

class WebhookToolProviderFactory : public ToolProviderFactory {
 public:
  WebhookToolProviderFactory(Profile* profile, WebhookToolService* service);
  ~WebhookToolProviderFactory() override;

  WebhookToolProviderFactory(const WebhookToolProviderFactory&) = delete;
  WebhookToolProviderFactory& operator=(const WebhookToolProviderFactory&) =
      delete;

  // ToolProviderFactory:
  std::unique_ptr<ToolProvider> CreateToolProvider() override;

 private:
  raw_ptr<Profile> profile_;
  raw_ptr<WebhookToolService> service_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WEBHOOK_TOOL_PROVIDER_FACTORY_H_

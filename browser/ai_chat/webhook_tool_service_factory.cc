// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/webhook_tool_service_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/ai_chat/webhook_tool_service.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
WebhookToolServiceFactory* WebhookToolServiceFactory::GetInstance() {
  static base::NoDestructor<WebhookToolServiceFactory> instance;
  return instance.get();
}

// static
WebhookToolService* WebhookToolServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  if (features::IsAIChatEnabled()) {
    return static_cast<WebhookToolService*>(
        GetInstance()->GetServiceForBrowserContext(context, true));
  }
  return nullptr;
}

WebhookToolServiceFactory::WebhookToolServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "WebhookToolServiceFactory",
          BrowserContextDependencyManager::GetInstance()) {}

WebhookToolServiceFactory::~WebhookToolServiceFactory() = default;

std::unique_ptr<KeyedService>
WebhookToolServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<WebhookToolService>(
      user_prefs::UserPrefs::Get(context));
}

}  // namespace ai_chat

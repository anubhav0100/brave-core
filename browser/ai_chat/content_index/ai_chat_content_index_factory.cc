// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
AiChatContentIndexFactory* AiChatContentIndexFactory::GetInstance() {
  static base::NoDestructor<AiChatContentIndexFactory> instance;
  return instance.get();
}

// static
AiChatContentIndex* AiChatContentIndexFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  if (features::IsAIChatEnabled()) {
    return static_cast<AiChatContentIndex*>(
        GetInstance()->GetServiceForBrowserContext(context, true));
  }
  return nullptr;
}

AiChatContentIndexFactory::AiChatContentIndexFactory()
    : BrowserContextKeyedServiceFactory(
          "AiChatContentIndexFactory",
          BrowserContextDependencyManager::GetInstance()) {}

AiChatContentIndexFactory::~AiChatContentIndexFactory() = default;

std::unique_ptr<KeyedService>
AiChatContentIndexFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<AiChatContentIndex>(context->GetPath());
}

}  // namespace ai_chat

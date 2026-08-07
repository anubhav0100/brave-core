// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_repository_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/ai_chat/workflows/workflow_repository.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
WorkflowRepositoryFactory* WorkflowRepositoryFactory::GetInstance() {
  static base::NoDestructor<WorkflowRepositoryFactory> instance;
  return instance.get();
}

// static
WorkflowRepository* WorkflowRepositoryFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  if (features::IsAIChatEnabled()) {
    return static_cast<WorkflowRepository*>(
        GetInstance()->GetServiceForBrowserContext(context, true));
  }
  return nullptr;
}

WorkflowRepositoryFactory::WorkflowRepositoryFactory()
    : BrowserContextKeyedServiceFactory(
          "WorkflowRepositoryFactory",
          BrowserContextDependencyManager::GetInstance()) {}

WorkflowRepositoryFactory::~WorkflowRepositoryFactory() = default;

std::unique_ptr<KeyedService>
WorkflowRepositoryFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<WorkflowRepository>(
      user_prefs::UserPrefs::Get(context));
}

}  // namespace ai_chat

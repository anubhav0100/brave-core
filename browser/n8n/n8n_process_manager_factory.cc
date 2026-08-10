// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/n8n/n8n_process_manager_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
N8nProcessManagerFactory* N8nProcessManagerFactory::GetInstance() {
  static base::NoDestructor<N8nProcessManagerFactory> instance;
  return instance.get();
}

// static
N8nProcessManager* N8nProcessManagerFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  return static_cast<N8nProcessManager*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

N8nProcessManagerFactory::N8nProcessManagerFactory()
    : BrowserContextKeyedServiceFactory(
          "N8nProcessManagerFactory",
          BrowserContextDependencyManager::GetInstance()) {}

N8nProcessManagerFactory::~N8nProcessManagerFactory() = default;

std::unique_ptr<KeyedService>
N8nProcessManagerFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<N8nProcessManager>(context);
}

bool N8nProcessManagerFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace ai_chat

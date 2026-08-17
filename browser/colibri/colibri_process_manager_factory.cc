// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/colibri/colibri_process_manager_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/colibri/colibri_process_manager.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
ColibriProcessManagerFactory* ColibriProcessManagerFactory::GetInstance() {
  static base::NoDestructor<ColibriProcessManagerFactory> instance;
  return instance.get();
}

// static
ColibriProcessManager* ColibriProcessManagerFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  return static_cast<ColibriProcessManager*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

ColibriProcessManagerFactory::ColibriProcessManagerFactory()
    : BrowserContextKeyedServiceFactory(
          "ColibriProcessManagerFactory",
          BrowserContextDependencyManager::GetInstance()) {}

ColibriProcessManagerFactory::~ColibriProcessManagerFactory() = default;

std::unique_ptr<KeyedService>
ColibriProcessManagerFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<ColibriProcessManager>(context);
}

}  // namespace ai_chat

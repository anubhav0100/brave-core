// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/delegation/delegation_process_manager_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/delegation/delegation_process_manager.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

// static
DelegationProcessManagerFactory*
DelegationProcessManagerFactory::GetInstance() {
  static base::NoDestructor<DelegationProcessManagerFactory> instance;
  return instance.get();
}

// static
DelegationProcessManager* DelegationProcessManagerFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  return static_cast<DelegationProcessManager*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

DelegationProcessManagerFactory::DelegationProcessManagerFactory()
    : BrowserContextKeyedServiceFactory(
          "DelegationProcessManagerFactory",
          BrowserContextDependencyManager::GetInstance()) {}

DelegationProcessManagerFactory::~DelegationProcessManagerFactory() = default;

std::unique_ptr<KeyedService>
DelegationProcessManagerFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<DelegationProcessManager>(context);
}

bool DelegationProcessManagerFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_FACTORY_H_
#define BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_FACTORY_H_

#include <memory>

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "components/keyed_service/core/keyed_service.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace ai_chat {

class DelegationProcessManager;

class DelegationProcessManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  DelegationProcessManagerFactory(const DelegationProcessManagerFactory&) =
      delete;
  DelegationProcessManagerFactory& operator=(
      const DelegationProcessManagerFactory&) = delete;

  static DelegationProcessManagerFactory* GetInstance();
  static DelegationProcessManager* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  friend base::NoDestructor<DelegationProcessManagerFactory>;

  DelegationProcessManagerFactory();
  ~DelegationProcessManagerFactory() override;

  // BrowserContextKeyedServiceFactory overrides:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  // Eager, same rationale as N8nProcessManagerFactory - the sidebar entry
  // should exist before the user has ever opened Delegation, but this does
  // NOT start the process itself.
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_DELEGATION_DELEGATION_PROCESS_MANAGER_FACTORY_H_

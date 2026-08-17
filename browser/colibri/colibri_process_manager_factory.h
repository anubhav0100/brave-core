// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_FACTORY_H_
#define BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_FACTORY_H_

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

class ColibriProcessManager;

class ColibriProcessManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  ColibriProcessManagerFactory(const ColibriProcessManagerFactory&) = delete;
  ColibriProcessManagerFactory& operator=(
      const ColibriProcessManagerFactory&) = delete;

  static ColibriProcessManagerFactory* GetInstance();
  static ColibriProcessManager* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  friend base::NoDestructor<ColibriProcessManagerFactory>;

  ColibriProcessManagerFactory();
  ~ColibriProcessManagerFactory() override;

  // BrowserContextKeyedServiceFactory overrides:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_COLIBRI_COLIBRI_PROCESS_MANAGER_FACTORY_H_

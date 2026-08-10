// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_FACTORY_H_
#define BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_FACTORY_H_

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

namespace computer_use {

class ComputerUseSessionState;

class ComputerUseSessionStateFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  ComputerUseSessionStateFactory(const ComputerUseSessionStateFactory&) =
      delete;
  ComputerUseSessionStateFactory& operator=(
      const ComputerUseSessionStateFactory&) = delete;

  static ComputerUseSessionStateFactory* GetInstance();
  static ComputerUseSessionState* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  friend base::NoDestructor<ComputerUseSessionStateFactory>;

  ComputerUseSessionStateFactory();
  ~ComputerUseSessionStateFactory() override;

  // BrowserContextKeyedServiceFactory overrides:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_FACTORY_H_

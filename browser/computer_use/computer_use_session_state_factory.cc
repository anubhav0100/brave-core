// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/computer_use_session_state_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace computer_use {

// static
ComputerUseSessionStateFactory* ComputerUseSessionStateFactory::GetInstance() {
  static base::NoDestructor<ComputerUseSessionStateFactory> instance;
  return instance.get();
}

// static
ComputerUseSessionState* ComputerUseSessionStateFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  return static_cast<ComputerUseSessionState*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

ComputerUseSessionStateFactory::ComputerUseSessionStateFactory()
    : BrowserContextKeyedServiceFactory(
          "ComputerUseSessionStateFactory",
          BrowserContextDependencyManager::GetInstance()) {}

ComputerUseSessionStateFactory::~ComputerUseSessionStateFactory() = default;

std::unique_ptr<KeyedService>
ComputerUseSessionStateFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<ComputerUseSessionState>(
      user_prefs::UserPrefs::Get(context), context->GetPath());
}

}  // namespace computer_use

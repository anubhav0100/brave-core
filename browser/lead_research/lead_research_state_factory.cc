// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/lead_research/lead_research_state_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/lead_research/lead_research_state.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace lead_research {

// static
LeadResearchStateFactory* LeadResearchStateFactory::GetInstance() {
  static base::NoDestructor<LeadResearchStateFactory> instance;
  return instance.get();
}

// static
LeadResearchState* LeadResearchStateFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  DCHECK(context);
  return static_cast<LeadResearchState*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

LeadResearchStateFactory::LeadResearchStateFactory()
    : BrowserContextKeyedServiceFactory(
          "LeadResearchStateFactory",
          BrowserContextDependencyManager::GetInstance()) {}

LeadResearchStateFactory::~LeadResearchStateFactory() = default;

std::unique_ptr<KeyedService>
LeadResearchStateFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<LeadResearchState>(
      user_prefs::UserPrefs::Get(context));
}

}  // namespace lead_research

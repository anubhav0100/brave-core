// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/colibri/colibri_service_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/ai_chat/model_service_factory.h"
#include "brave/components/ai_chat/core/browser/colibri/colibri_model_fetcher.h"
#include "brave/components/ai_chat/core/browser/colibri/colibri_service.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

namespace ai_chat {

// static
ColibriServiceFactory* ColibriServiceFactory::GetInstance() {
  static base::NoDestructor<ColibriServiceFactory> instance;
  return instance.get();
}

// static
ColibriService* ColibriServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<ColibriService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
ProfileSelections ColibriServiceFactory::CreateProfileSelections() {
  if (!features::IsAIChatEnabled()) {
    return ProfileSelections::BuildNoProfilesSelected();
  }
  return ProfileSelections::Builder()
      .WithRegular(ProfileSelection::kOriginalOnly)
      .Build();
}

ColibriServiceFactory::ColibriServiceFactory()
    : ProfileKeyedServiceFactory("ColibriServiceFactory",
                                 CreateProfileSelections()) {
  DependsOn(ModelServiceFactory::GetInstance());
}

ColibriServiceFactory::~ColibriServiceFactory() = default;

std::unique_ptr<KeyedService>
ColibriServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto url_loader_factory = context->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto* model_service = ModelServiceFactory::GetForBrowserContext(context);
  auto* prefs = user_prefs::UserPrefs::Get(context);

  std::unique_ptr<ColibriModelFetcher> model_fetcher;
  if (model_service && prefs) {
    // Pass nullptr as delegate initially; ColibriService will set itself
    // as the delegate when it takes ownership.
    model_fetcher = std::make_unique<ColibriModelFetcher>(
        *model_service, prefs, /*delegate=*/nullptr);
  }

  auto colibri_service = std::make_unique<ColibriService>(
      url_loader_factory, std::move(model_fetcher));

  return colibri_service;
}

}  // namespace ai_chat

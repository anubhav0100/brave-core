// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_COLIBRI_COLIBRI_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_AI_CHAT_COLIBRI_COLIBRI_SERVICE_FACTORY_H_

#include <memory>

#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace content {
class BrowserContext;
}  // namespace content

namespace base {

template <typename T>
class NoDestructor;
}  // namespace base

namespace ai_chat {

class ColibriService;

class ColibriServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ColibriServiceFactory* GetInstance();
  static ColibriService* GetForProfile(Profile* profile);

 private:
  friend base::NoDestructor<ColibriServiceFactory>;

  static ProfileSelections CreateProfileSelections();

  ColibriServiceFactory();
  ~ColibriServiceFactory() override;

  // ProfileKeyedServiceFactory overrides:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};
}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_COLIBRI_COLIBRI_SERVICE_FACTORY_H_

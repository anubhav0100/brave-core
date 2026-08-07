// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/webhook_tool_provider_factory.h"

#include "brave/browser/ai_chat/webhook_tool_provider.h"

namespace ai_chat {

WebhookToolProviderFactory::WebhookToolProviderFactory(
    Profile* profile,
    WebhookToolService* service)
    : profile_(profile), service_(service) {}

WebhookToolProviderFactory::~WebhookToolProviderFactory() = default;

std::unique_ptr<ToolProvider>
WebhookToolProviderFactory::CreateToolProvider() {
  return std::make_unique<WebhookToolProvider>(profile_, service_);
}

}  // namespace ai_chat

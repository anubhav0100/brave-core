// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/webhook_tool_provider.h"

#include <utility>

#include "brave/browser/ai_chat/webhook_tool_service.h"
#include "brave/components/ai_chat/core/browser/tools/webhook_tool.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace ai_chat {

WebhookToolProvider::WebhookToolProvider(Profile* profile,
                                         WebhookToolService* service) {
  if (!service || !profile) {
    return;
  }
  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  for (const auto& config : service->GetTools()) {
    if (!config.enabled || config.name.empty() || config.url.empty()) {
      continue;
    }
    std::vector<WebhookTool::Parameter> parameters;
    for (const auto& param : config.parameters) {
      parameters.push_back({param.name, param.description, param.required});
    }
    tools_.push_back(std::make_unique<WebhookTool>(
        config.name, config.description, config.url, config.secret,
        std::move(parameters), url_loader_factory));
  }
}

WebhookToolProvider::~WebhookToolProvider() = default;

std::vector<base::WeakPtr<Tool>> WebhookToolProvider::GetTools() {
  std::vector<base::WeakPtr<Tool>> tools;
  for (const auto& tool : tools_) {
    tools.push_back(tool->GetWeakPtr());
  }
  return tools;
}

}  // namespace ai_chat

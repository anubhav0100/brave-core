// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_WEBHOOK_TOOL_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_WEBHOOK_TOOL_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace api_request_helper {
class APIRequestHelper;
class APIRequestResult;
}  // namespace api_request_helper

namespace ai_chat {

// A Tool backed by a user-configured webhook (Settings -> AI Assistant ->
// Custom tools): using it POSTs the model's chosen arguments to a URL the
// user configured, and the response body becomes the tool's result text.
class WebhookTool : public Tool {
 public:
  struct Parameter {
    std::string name;
    std::string description;
    bool required = false;
  };

  WebhookTool(
      std::string name,
      std::string description,
      std::string url,
      std::string secret,
      std::vector<Parameter> parameters,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~WebhookTool() override;

  WebhookTool(const WebhookTool&) = delete;
  WebhookTool& operator=(const WebhookTool&) = delete;

  // Tool overrides
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnResponse(UseToolCallback callback,
                  api_request_helper::APIRequestResult result);

  std::string name_;
  std::string description_;
  std::string url_;
  std::string secret_;
  std::vector<Parameter> parameters_;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;
  base::WeakPtrFactory<WebhookTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_TOOLS_WEBHOOK_TOOL_H_

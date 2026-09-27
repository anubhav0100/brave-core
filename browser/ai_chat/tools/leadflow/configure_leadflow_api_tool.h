// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_CONFIGURE_LEADFLOW_API_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_CONFIGURE_LEADFLOW_API_TOOL_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/tools/leadflow/leadflow_api_client.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// One-time setup tool: points the other leadflow_* tools at a running
// LeadFlow API server (see leadflow-api/ at the repo root). With no
// api_key argument, bootstraps the very first key from a fresh server
// (only works once - see BootstrapController's own comment); with one,
// just stores it directly (recovery path once a server has already been
// bootstrapped by someone/something else).
class ConfigureLeadflowApiTool : public Tool {
 public:
  explicit ConfigureLeadflowApiTool(content::BrowserContext* browser_context);
  ~ConfigureLeadflowApiTool() override;
  ConfigureLeadflowApiTool(const ConfigureLeadflowApiTool&) = delete;
  ConfigureLeadflowApiTool& operator=(const ConfigureLeadflowApiTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json, UseToolCallback callback) override;

  base::WeakPtr<Tool> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  void OnHealthCheckResult(UseToolCallback callback,
                           std::string base_url,
                           bool success,
                           base::Value result);
  void OnBootstrapResult(UseToolCallback callback, bool success, std::string message);

  std::unique_ptr<leadflow::LeadFlowApiClient> client_;
  base::WeakPtrFactory<ConfigureLeadflowApiTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_CONFIGURE_LEADFLOW_API_TOOL_H_

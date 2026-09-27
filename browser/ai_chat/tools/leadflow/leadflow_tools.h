// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_TOOLS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/tools/leadflow/leadflow_api_client.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}

namespace ai_chat {

enum class LeadflowOperation {
  kAddLead,
  kSearchLeads,
  kUpdateLead,
  kSendWhatsAppMessage,
  kSendWhatsAppTemplate,
  kGetLeadMessages,
};

// Small REST adapters exposed to Leo. Business guardrails remain in the API,
// so browser prompts cannot bypass consent, the 24-hour window, or API-key
// permissions.
class LeadflowTool : public Tool {
 public:
  LeadflowTool(content::BrowserContext* browser_context,
               LeadflowOperation operation);
  ~LeadflowTool() override;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

  base::WeakPtr<Tool> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  void OnResult(UseToolCallback callback, bool success, base::Value result);

  LeadflowOperation operation_;
  std::unique_ptr<leadflow::LeadFlowApiClient> client_;
  base::WeakPtrFactory<LeadflowTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_TOOLS_H_

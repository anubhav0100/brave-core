// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_CLOSE_RDP_SESSION_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_CLOSE_RDP_SESSION_TOOL_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Disconnects the active RDP session, if any. Routine - unlike opening a
// connection, closing one doesn't need per-call confirmation.
class CloseRdpSessionTool : public Tool {
 public:
  explicit CloseRdpSessionTool(content::BrowserContext* browser_context);
  ~CloseRdpSessionTool() override;

  CloseRdpSessionTool(const CloseRdpSessionTool&) = delete;
  CloseRdpSessionTool& operator=(const CloseRdpSessionTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  bool IsAgentTool() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_CLOSE_RDP_SESSION_TOOL_H_

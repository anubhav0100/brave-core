// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_RDP_SESSION_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_RDP_SESSION_TOOL_H_

#include <string>
#include <variant>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Opens a visible RDP session to a host the user names explicitly - see
// brave-ai-computer-use.md, Phase 3. Unlike the desktop_* input tools,
// this ALWAYS requires a fresh permission challenge naming the target
// host, every single call, regardless of any prior consent - the safety
// architecture is explicit that RDP connections are never silently
// autonomous ("Opening any RDP connection always requires an explicit,
// single-purpose confirmation... regardless of the local autonomy
// setting"). Never touches a password: authentication is handled entirely
// by the RDP control's own native prompt or Windows Credential Manager
// (see rdp_session.h).
class OpenRdpSessionTool : public Tool {
 public:
  explicit OpenRdpSessionTool(content::BrowserContext* browser_context);
  ~OpenRdpSessionTool() override;

  OpenRdpSessionTool(const OpenRdpSessionTool&) = delete;
  OpenRdpSessionTool& operator=(const OpenRdpSessionTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  bool IsAgentTool() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  std::variant<bool, mojom::PermissionChallengePtr>
  RequiresUserInteractionBeforeHandling(
      const mojom::ToolUseEvent& tool_use) const override;
  void UserPermissionGranted(const std::string& tool_use_id) override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnConnectResult(UseToolCallback callback,
                       bool success,
                       std::string error_message);

  raw_ptr<content::BrowserContext> browser_context_;

  base::WeakPtrFactory<OpenRdpSessionTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_RDP_SESSION_TOOL_H_

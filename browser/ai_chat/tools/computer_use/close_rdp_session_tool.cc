// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/close_rdp_session_tool.h"

#include <utility>

#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

CloseRdpSessionTool::CloseRdpSessionTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CloseRdpSessionTool::~CloseRdpSessionTool() = default;

std::string_view CloseRdpSessionTool::Name() const {
  return mojom::kCloseRdpSessionToolName;
}

std::string_view CloseRdpSessionTool::Description() const {
  return "Disconnects the active RDP session, if any.";
}

bool CloseRdpSessionTool::IsAgentTool() const {
  return true;
}

void CloseRdpSessionTool::UseTool(const std::string& input_json,
                                  UseToolCallback callback) {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  bool was_active = state->IsRdpActive();
  state->DisconnectRdp();
  std::move(callback).Run(
      CreateContentBlocksForText(was_active
                                     ? "RDP session disconnected."
                                     : "No active RDP session to disconnect."),
      {});
}

}  // namespace ai_chat

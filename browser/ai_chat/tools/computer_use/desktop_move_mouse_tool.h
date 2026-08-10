// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_MOVE_MOUSE_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_MOVE_MOUSE_TOOL_H_

#include <string>

#include "brave/browser/ai_chat/tools/computer_use/desktop_input_tool_base.h"

namespace ai_chat {

// Moves the OS mouse pointer to absolute desktop coordinates. Always
// routine (per brave-ai-computer-use.md's safety architecture, mouse move
// alone can't cause any effect) - never risk-gated beyond the shared
// one-time desktop-input consent.
class DesktopMoveMouseTool : public DesktopInputToolBase {
 public:
  explicit DesktopMoveMouseTool(content::BrowserContext* browser_context);
  ~DesktopMoveMouseTool() override;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 protected:
  std::optional<std::pair<std::string, std::string>> GetActionContext(
      const std::string& arguments_json) const override;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_MOVE_MOUSE_TOOL_H_

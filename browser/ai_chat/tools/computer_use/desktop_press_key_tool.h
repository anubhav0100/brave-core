// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_PRESS_KEY_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_PRESS_KEY_TOOL_H_

#include <string>

#include "brave/browser/ai_chat/tools/computer_use/desktop_input_tool_base.h"

namespace ai_chat {

// Presses a named key or modifier combo (e.g. "Enter", "Ctrl+C") into
// whatever currently has keyboard focus, anywhere on the desktop.
// Risk-gated on both the target app and the key content itself (e.g.
// "Delete" combined with a sensitive-keyword match, or targeting a
// sensitive system utility).
class DesktopPressKeyTool : public DesktopInputToolBase {
 public:
  explicit DesktopPressKeyTool(content::BrowserContext* browser_context);
  ~DesktopPressKeyTool() override;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_PRESS_KEY_TOOL_H_

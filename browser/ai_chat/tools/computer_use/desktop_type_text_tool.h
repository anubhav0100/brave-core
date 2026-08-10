// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_TYPE_TEXT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_TYPE_TEXT_TOOL_H_

#include <string>

#include "brave/browser/ai_chat/tools/computer_use/desktop_input_tool_base.h"

namespace ai_chat {

// Types text into whatever currently has keyboard focus, anywhere on the
// desktop - not just this browser. Risk-gated on both the target app (per
// DesktopInputToolBase) and the typed content itself (sensitive keywords
// like "format"/"delete"/"sudo" always require confirmation).
class DesktopTypeTextTool : public DesktopInputToolBase {
 public:
  explicit DesktopTypeTextTool(content::BrowserContext* browser_context);
  ~DesktopTypeTextTool() override;

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

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_TYPE_TEXT_TOOL_H_

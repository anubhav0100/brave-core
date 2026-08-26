// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_COMPUTER_USE_PAGE_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_COMPUTER_USE_PAGE_TOOL_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Opens (or switches to an already-open tab showing) chrome://computer-use,
// the status/settings/RDP page for this profile's computer-use session. Use
// this whenever the user asks to see, open, or check the computer-use page.
class OpenComputerUsePageTool : public Tool {
 public:
  explicit OpenComputerUsePageTool(content::BrowserContext* browser_context);
  ~OpenComputerUsePageTool() override;

  OpenComputerUsePageTool(const OpenComputerUsePageTool&) = delete;
  OpenComputerUsePageTool& operator=(const OpenComputerUsePageTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_OPEN_COMPUTER_USE_PAGE_TOOL_H_

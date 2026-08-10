// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_GET_DESKTOP_SCREENSHOT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_GET_DESKTOP_SCREENSHOT_TOOL_H_

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace computer_use {
class ComputerUseSessionState;
}  // namespace computer_use

namespace ai_chat {

class DesktopCaptureSession;

// Exposes a still screenshot of the entire host desktop (all monitors, any
// app - not just the browser) to the AI, as the observe half of the
// computer-use feature's observe-decide-act loop (see
// brave-ai-computer-use.md, Phase 1). Agent-mode only (IsAgentTool()),
// matching the existing viewport-scoped Actor tools' convention for
// powerful action-capable tools, and requires one-time explicit user
// permission per conversation before the first capture - mirrors
// HistorySearchTool's exact permission-challenge pattern, since both are
// "read something sensitive and hand it to the model" tools.
class GetDesktopScreenshotTool : public Tool {
 public:
  explicit GetDesktopScreenshotTool(content::BrowserContext* browser_context);
  ~GetDesktopScreenshotTool() override;

  GetDesktopScreenshotTool(const GetDesktopScreenshotTool&) = delete;
  GetDesktopScreenshotTool& operator=(const GetDesktopScreenshotTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  bool IsAgentTool() const override;
  std::variant<bool, mojom::PermissionChallengePtr>
  RequiresUserInteractionBeforeHandling(
      const mojom::ToolUseEvent& tool_use) const override;
  void UserPermissionGranted(const std::string& tool_use_id) override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnScreenshotCaptured(UseToolCallback callback,
                            bool success,
                            std::vector<uint8_t> png_bytes);

  raw_ptr<content::BrowserContext> browser_context_;
  std::unique_ptr<DesktopCaptureSession> capture_session_;
  bool user_has_granted_permission_ = false;

  base::WeakPtrFactory<GetDesktopScreenshotTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_GET_DESKTOP_SCREENSHOT_TOOL_H_

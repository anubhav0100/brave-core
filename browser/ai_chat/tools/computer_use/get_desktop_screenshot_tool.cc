// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/get_desktop_screenshot_tool.h"

#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/computer_use/desktop_capture_session.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace ai_chat {

GetDesktopScreenshotTool::GetDesktopScreenshotTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context),
      capture_session_(std::make_unique<DesktopCaptureSession>()) {}

GetDesktopScreenshotTool::~GetDesktopScreenshotTool() = default;

std::string_view GetDesktopScreenshotTool::Name() const {
  return mojom::kGetDesktopScreenshotToolName;
}

std::string_view GetDesktopScreenshotTool::Description() const {
  return "Captures a screenshot of the user's entire desktop - every "
         "monitor, whatever app is on screen, not just this browser. Use "
         "this to see what's currently on screen before deciding on a "
         "computer-use action. Requires the user's one-time permission the "
         "first time it's used in a conversation.";
}

bool GetDesktopScreenshotTool::IsAgentTool() const {
  return true;
}

std::variant<bool, mojom::PermissionChallengePtr>
GetDesktopScreenshotTool::RequiresUserInteractionBeforeHandling(
    const mojom::ToolUseEvent& tool_use) const {
  if (user_has_granted_permission_) {
    return false;
  }
  // The user-facing wording lives in get_tool_permission_implications.tsx
  // (i18n) - this side only needs to surface a non-null challenge. See
  // HistorySearchTool for the identical pattern this mirrors.
  return mojom::PermissionChallenge::New(/*assessment=*/std::nullopt,
                                         /*plan=*/std::nullopt);
}

void GetDesktopScreenshotTool::UserPermissionGranted(
    const std::string& tool_use_id) {
  user_has_granted_permission_ = true;
}

void GetDesktopScreenshotTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  capture_session_->CaptureScreenshot(
      base::BindOnce(&GetDesktopScreenshotTool::OnScreenshotCaptured,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void GetDesktopScreenshotTool::OnScreenshotCaptured(
    UseToolCallback callback,
    bool success,
    std::vector<uint8_t> png_bytes) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: failed to capture a desktop screenshot."),
        {});
    return;
  }
  std::string data_url_string = base::StrCat(
      {"data:image/png;base64,", base::Base64Encode(png_bytes)});
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      browser_context_)
      ->SetLatestFrame(data_url_string);
  std::move(callback).Run(
      CreateContentBlocksForImage(GURL(data_url_string)), {});
}

}  // namespace ai_chat

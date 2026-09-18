// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/get_desktop_screenshot_tool.h"

#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/computer_use/desktop_capture_session.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "build/build_config.h"
#include "content/public/browser/browser_context.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {
// Matches DesktopInputToolBase::kPropertyTarget/kTargetValueRdp/
// kTargetValueLocalDesktop (desktop_input_tool_base.h) - kept as separate
// literals here rather than a shared include, since that header (and RDP
// itself) is Windows-only, while this tool is cross-platform.
constexpr char kPropertyTarget[] = "target";
constexpr char kTargetValueRdp[] = "rdp";
constexpr char kTargetValueLocalDesktop[] = "local_desktop";
}  // namespace

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
         "monitor, whatever app is on screen, not just this browser. If an "
         "RDP session is currently open (see open_rdp_session), this "
         "instead captures that remote session's own screen - use it for "
         "that, not a browser tab screenshot, while RDP is active. Use "
         "this to see what's currently on screen before deciding on a "
         "computer-use action. Requires the user's one-time permission the "
         "first time it's used in a conversation.";
}

bool GetDesktopScreenshotTool::IsAgentTool() const {
  return true;
}

std::optional<base::DictValue> GetDesktopScreenshotTool::InputProperties()
    const {
  return CreateInputProperties({
      {kPropertyTarget,
       StringProperty(
           "Which surface to capture - \"rdp\" for the active RDP "
           "session, \"local_desktop\" for this machine's own desktop. "
           "Omit to auto-target whichever is currently active.",
           std::vector<std::string>{kTargetValueRdp,
                                    kTargetValueLocalDesktop})},
  });
}

std::variant<bool, mojom::PermissionChallengePtr>
GetDesktopScreenshotTool::RequiresUserInteractionBeforeHandling(
    const mojom::ToolUseEvent& tool_use) const {
  if (user_has_granted_permission_) {
    return false;
  }
  // Persisted opt-in (chrome://computer-use Settings toggle) to skip this
  // challenge for every future conversation too, not just this one - see
  // ComputerUseSessionState::GetAlwaysAllowDesktopScreenshot.
  if (computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_)
          ->GetAlwaysAllowDesktopScreenshot()) {
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
#if BUILDFLAG(IS_WIN)
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  bool rdp_active = state->IsRdpActive();
  bool use_rdp = rdp_active;
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* target =
      input ? input->FindString(kPropertyTarget) : nullptr;
  if (target && *target == kTargetValueRdp) {
    if (!rdp_active) {
      std::move(callback).Run(
          CreateContentBlocksForText(
              "Error: target=\"rdp\" was requested but no RDP session is "
              "currently active. Call open_rdp_session first."),
          {});
      return;
    }
    use_rdp = true;
  } else if (target && *target == kTargetValueLocalDesktop) {
    use_rdp = false;
  }
  // While RDP is active, its session window is hidden (see rdp_session.h)
  // and never appears in a full-desktop capture at all - the RDP capture
  // timer (ComputerUseSessionState) already keeps a fresh, window-specific
  // capture of it (~5x/sec), so reuse that instead of taking a second,
  // redundant full-desktop capture that wouldn't show it anyway.
  if (use_rdp && !state->GetLatestFrameDataUrl().empty()) {
    std::move(callback).Run(
        CreateContentBlocksForImage(GURL(state->GetLatestFrameDataUrl())),
        {});
    return;
  }
#endif
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

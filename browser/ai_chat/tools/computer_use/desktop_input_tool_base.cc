// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_input_tool_base.h"

#include <utility>

#include "base/strings/strcat.h"
#include "brave/browser/computer_use/action_risk_classifier.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/computer_use/input_injector.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"

namespace ai_chat {

DesktopInputToolBase::DesktopInputToolBase(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context),
      input_injector_(std::make_unique<InputInjector>()) {}

DesktopInputToolBase::~DesktopInputToolBase() = default;

bool DesktopInputToolBase::IsAgentTool() const {
  return true;
}

std::variant<bool, mojom::PermissionChallengePtr>
DesktopInputToolBase::RequiresUserInteractionBeforeHandling(
    const mojom::ToolUseEvent& tool_use) const {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  // The persistent "Allow full desktop control" setting (chrome://
  // computer-use's Settings toggle) is itself an explicit, standing
  // consent to this whole feature - skip the one-time in-conversation
  // challenge below when it's on, the same way AlwaysAllowDesktopScreenshot
  // already skips get_desktop_screenshot's own per-conversation prompt.
  // Per-action risk reconfirmation (below) is a distinct, ongoing safety
  // layer and still applies regardless, so this alone doesn't let anything
  // risky through silently - it only removes the redundant "are you sure
  // you want this feature at all" ask once the user has already answered
  // that persistently.
  if (!state->HasInputConsent() && !state->GetFullDesktopControlEnabled()) {
    // First-ever use of any desktop_* tool this conversation. The
    // user-facing wording lives in get_tool_permission_implications.tsx.
    return mojom::PermissionChallenge::New(/*assessment=*/std::nullopt,
                                           /*plan=*/std::nullopt);
  }

  auto context = GetActionContext(tool_use.arguments_json);
  if (context) {
    auto risk = computer_use::ClassifyDesktopAction(context->first,
                                                     context->second, state);
    if (risk.is_risky) {
      return mojom::PermissionChallenge::New(/*assessment=*/std::nullopt,
                                             /*plan=*/risk.reason);
    }
  }

  return false;
}

void DesktopInputToolBase::UserPermissionGranted(
    const std::string& tool_use_id) {
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      browser_context_)
      ->GrantInputConsent();
}

bool DesktopInputToolBase::IsEmergencyStopped() const {
  return computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
             browser_context_)
      ->IsEmergencyStopped();
}

void DesktopInputToolBase::MarkAppInteracted(
    const std::string& process_name) {
  if (process_name.empty()) {
    return;
  }
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      browser_context_)
      ->MarkAppInteracted(process_name);
}

std::string DesktopInputToolBase::GetTargetProcessName(int x, int y) const {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  if (state->IsRdpActive()) {
    return base::StrCat({"rdp:", state->GetRdpTargetHost()});
  }
  return computer_use::GetProcessNameAtPoint(x, y);
}

std::optional<std::string> DesktopInputToolBase::ResolveTargetOverride(
    const std::string* target_argument,
    bool* out_use_rdp) const {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  bool rdp_active = state->IsRdpActive();

  // Full local-desktop control (real OS-level SendInput on this machine,
  // capable of acting on any app) is a separate, persistent, explicit
  // opt-in set from chrome://computer-use's Settings toggle - distinct
  // from RDP, which already has its own per-connection consent via
  // ConnectRdp/open_rdp_session_tool and needs no extra gate here. Only
  // checked once a call is actually about to resolve to the local desktop.
  auto check_local_desktop_allowed = [&]() -> std::optional<std::string> {
    if (state->GetFullDesktopControlEnabled()) {
      return std::nullopt;
    }
    return "Error: full desktop control is turned off. Ask the user to "
           "turn on \"Allow full desktop control\" on "
           "chrome://computer-use before desktop input tools can act on "
           "the local desktop.";
  };

  if (!target_argument || target_argument->empty()) {
    *out_use_rdp = rdp_active;
    return *out_use_rdp ? std::nullopt : check_local_desktop_allowed();
  }
  if (*target_argument == kTargetValueRdp) {
    if (!rdp_active) {
      return "Error: target=\"rdp\" was requested but no RDP session is "
             "currently active. Call open_rdp_session first.";
    }
    *out_use_rdp = true;
    return std::nullopt;
  }
  if (*target_argument == kTargetValueLocalDesktop) {
    if (auto error = check_local_desktop_allowed()) {
      return error;
    }
    *out_use_rdp = false;
    return std::nullopt;
  }
  // Shouldn't normally happen - the "target" property's schema restricts
  // it to the two values above - but fall back to auto rather than erroring
  // on an unrecognized value from a non-conforming caller.
  *out_use_rdp = rdp_active;
  return *out_use_rdp ? std::nullopt : check_local_desktop_allowed();
}

std::string DesktopInputToolBase::GetForegroundTargetProcessName() const {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  if (state->IsRdpActive()) {
    return base::StrCat({"rdp:", state->GetRdpTargetHost()});
  }
  return computer_use::GetForegroundProcessName();
}

}  // namespace ai_chat

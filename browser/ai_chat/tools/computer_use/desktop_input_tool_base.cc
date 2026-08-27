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
  if (!state->HasInputConsent()) {
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

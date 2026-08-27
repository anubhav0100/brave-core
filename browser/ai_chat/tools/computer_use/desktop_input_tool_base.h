// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_INPUT_TOOL_BASE_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_INPUT_TOOL_BASE_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class InputInjector;

// Shared consent + risk-gating logic for all `desktop_*` OS-input tools
// (move mouse, click, scroll, type text, press key) - see
// brave-ai-computer-use.md, Phase 2. One implementation of the safety gate
// shared by all five, rather than five copies that could drift apart:
//
// - First use of ANY desktop_* tool this conversation requires one-time
//   explicit consent (separate from, and beyond, get_desktop_screenshot's
//   own consent - controlling the desktop is a bigger step than viewing
//   it), shared across all five so the user isn't asked five times.
// - After that, each individual call is still freshly risk-assessed
//   (`GetActionContext`, implemented per-tool) - routine actions proceed
//   without confirmation, but a defined set of risky ones always pause for
//   it, every time, regardless of prior consent.
class DesktopInputToolBase : public Tool {
 public:
  explicit DesktopInputToolBase(content::BrowserContext* browser_context);
  ~DesktopInputToolBase() override;

  DesktopInputToolBase(const DesktopInputToolBase&) = delete;
  DesktopInputToolBase& operator=(const DesktopInputToolBase&) = delete;

  bool IsAgentTool() const override;
  std::variant<bool, mojom::PermissionChallengePtr>
  RequiresUserInteractionBeforeHandling(
      const mojom::ToolUseEvent& tool_use) const override;
  void UserPermissionGranted(const std::string& tool_use_id) override;

 protected:
  // Parses `arguments_json` for this tool's own schema and returns the
  // lowercase process image name this action would target (empty if it
  // can't be determined) plus a human-readable detail string (typed text /
  // key combo, empty for mouse actions) for keyword risk-checking. Returns
  // nullopt to skip risk assessment entirely (used by move-mouse, which
  // the design doc calls out as always routine).
  virtual std::optional<std::pair<std::string, std::string>> GetActionContext(
      const std::string& arguments_json) const = 0;

  // True if the emergency stop (global hotkey or the WebUI's Stop button)
  // has been triggered and not yet resumed - concrete tools must check
  // this at the top of UseTool() and refuse to inject anything if true.
  bool IsEmergencyStopped() const;

  // Records that this app was actually acted on, so future actions
  // against it aren't flagged as "first time seeing this app." Call after
  // a successful injected action.
  void MarkAppInteracted(const std::string& process_name);

  // Process-name identifiers for GetActionContext()/MarkAppInteracted(),
  // aware of RDP: when an RDP session is active, the local desktop's
  // window-at-point/foreground-window lookups (action_risk_classifier.h)
  // are meaningless - the coordinates/focus being acted on refer to the
  // RDP session's own hidden window's content, not anything actually
  // visible on the local desktop. A synthetic "rdp:<host>" identifier is
  // used instead in that case, so the "first action against this app"
  // risk check still behaves sensibly, scoped to the RDP target rather
  // than whatever unrelated local window happens to occupy those pixels.
  std::string GetTargetProcessName(int x, int y) const;
  std::string GetForegroundTargetProcessName() const;

  raw_ptr<content::BrowserContext> browser_context_;
  std::unique_ptr<InputInjector> input_injector_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_COMPUTER_USE_DESKTOP_INPUT_TOOL_BASE_H_

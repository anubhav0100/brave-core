// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_move_mouse_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/computer_use/input_injector.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyX[] = "x";
constexpr char kPropertyY[] = "y";
}  // namespace

DesktopMoveMouseTool::DesktopMoveMouseTool(
    content::BrowserContext* browser_context)
    : DesktopInputToolBase(browser_context) {}

DesktopMoveMouseTool::~DesktopMoveMouseTool() = default;

std::string_view DesktopMoveMouseTool::Name() const {
  return mojom::kDesktopMoveMouseToolName;
}

std::string_view DesktopMoveMouseTool::Description() const {
  return "Moves the OS mouse pointer to absolute desktop pixel coordinates "
         "(0,0 is the top-left of the primary display) - anywhere on "
         "screen, not just this browser. Does not click. Use "
         "get_desktop_screenshot first to see what's currently on screen "
         "and figure out target coordinates.";
}

std::optional<base::DictValue> DesktopMoveMouseTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyX, IntegerProperty("X coordinate in desktop pixels")},
      {kPropertyY, IntegerProperty("Y coordinate in desktop pixels")},
  });
}

std::optional<std::vector<std::string>>
DesktopMoveMouseTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyX, kPropertyY});
}

std::optional<std::pair<std::string, std::string>>
DesktopMoveMouseTool::GetActionContext(
    const std::string& arguments_json) const {
  return std::nullopt;
}

void DesktopMoveMouseTool::UseTool(const std::string& input_json,
                                   UseToolCallback callback) {
  if (IsEmergencyStopped()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: the user has triggered the emergency stop. They must "
            "click Resume on chrome://computer-use before further desktop "
            "input actions can run."),
        {});
    return;
  }

  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  auto x = input ? input->FindInt(kPropertyX) : std::nullopt;
  auto y = input ? input->FindInt(kPropertyY) : std::nullopt;
  if (!x || !y) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or invalid 'x'/'y'"), {});
    return;
  }

  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  bool success;
  if (state->IsRdpActive()) {
    state->SendRdpMouseEvent(*x, *y, 0, 0);
    success = true;
  } else {
    success = input_injector_->MoveMouse(*x, *y);
  }
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Mouse moved."
                                         : "Error: failed to move the mouse."),
      {});
}

}  // namespace ai_chat

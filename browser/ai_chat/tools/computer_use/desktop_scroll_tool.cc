// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_scroll_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "brave/browser/computer_use/action_risk_classifier.h"
#include "brave/browser/computer_use/input_injector.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyX[] = "x";
constexpr char kPropertyY[] = "y";
constexpr char kPropertyDeltaX[] = "delta_x";
constexpr char kPropertyDeltaY[] = "delta_y";
}  // namespace

DesktopScrollTool::DesktopScrollTool(content::BrowserContext* browser_context)
    : DesktopInputToolBase(browser_context) {}

DesktopScrollTool::~DesktopScrollTool() = default;

std::string_view DesktopScrollTool::Name() const {
  return mojom::kDesktopScrollToolName;
}

std::string_view DesktopScrollTool::Description() const {
  return "Scrolls the mouse wheel at absolute desktop pixel coordinates - "
         "anywhere on screen, not just this browser. Positive delta_y "
         "scrolls up, positive delta_x scrolls right; 120 is one notch.";
}

std::optional<base::DictValue> DesktopScrollTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyX, IntegerProperty("X coordinate in desktop pixels")},
      {kPropertyY, IntegerProperty("Y coordinate in desktop pixels")},
      {kPropertyDeltaY,
       IntegerProperty(
           "Vertical scroll amount, positive = up, negative = down "
           "(120 = one notch)")},
      {kPropertyDeltaX,
       IntegerProperty("Horizontal scroll amount, positive = right - "
                       "defaults to 0")},
  });
}

std::optional<std::vector<std::string>>
DesktopScrollTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>(
      {kPropertyX, kPropertyY, kPropertyDeltaY});
}

std::optional<std::pair<std::string, std::string>>
DesktopScrollTool::GetActionContext(const std::string& arguments_json) const {
  auto input = base::JSONReader::ReadDict(arguments_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  auto x = input ? input->FindInt(kPropertyX) : std::nullopt;
  auto y = input ? input->FindInt(kPropertyY) : std::nullopt;
  if (!x || !y) {
    return std::nullopt;
  }
  return std::make_pair(computer_use::GetProcessNameAtPoint(*x, *y),
                        std::string());
}

void DesktopScrollTool::UseTool(const std::string& input_json,
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
  int delta_x = input ? input->FindInt(kPropertyDeltaX).value_or(0) : 0;
  int delta_y = input ? input->FindInt(kPropertyDeltaY).value_or(0) : 0;

  bool success = input_injector_->Scroll(*x, *y, delta_x, delta_y);
  if (success) {
    MarkAppInteracted(computer_use::GetProcessNameAtPoint(*x, *y));
  }
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Scrolled."
                                         : "Error: failed to scroll."),
      {});
}

}  // namespace ai_chat

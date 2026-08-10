// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_click_tool.h"

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
constexpr char kPropertyButton[] = "button";
constexpr char kPropertyDoubleClick[] = "double_click";
}  // namespace

DesktopClickTool::DesktopClickTool(content::BrowserContext* browser_context)
    : DesktopInputToolBase(browser_context) {}

DesktopClickTool::~DesktopClickTool() = default;

std::string_view DesktopClickTool::Name() const {
  return mojom::kDesktopClickToolName;
}

std::string_view DesktopClickTool::Description() const {
  return "Clicks the mouse at absolute desktop pixel coordinates - "
         "anywhere on screen, not just this browser. Use "
         "get_desktop_screenshot first to see what's currently on screen "
         "and figure out target coordinates.";
}

std::optional<base::DictValue> DesktopClickTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyX, IntegerProperty("X coordinate in desktop pixels")},
      {kPropertyY, IntegerProperty("Y coordinate in desktop pixels")},
      {kPropertyButton,
       StringProperty("Mouse button - defaults to left",
                      std::vector<std::string>{"left", "right", "middle"})},
      {kPropertyDoubleClick,
       BooleanProperty("Whether to double-click - defaults to false")},
  });
}

std::optional<std::vector<std::string>>
DesktopClickTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyX, kPropertyY});
}

std::optional<std::pair<std::string, std::string>>
DesktopClickTool::GetActionContext(const std::string& arguments_json) const {
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

void DesktopClickTool::UseTool(const std::string& input_json,
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
  const std::string* button_ptr =
      input ? input->FindString(kPropertyButton) : nullptr;
  std::string button = button_ptr ? *button_ptr : "left";
  bool double_click =
      input && input->FindBool(kPropertyDoubleClick).value_or(false);

  bool success = input_injector_->Click(*x, *y, button, double_click);
  if (success) {
    MarkAppInteracted(computer_use::GetProcessNameAtPoint(*x, *y));
  }
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Clicked."
                                         : "Error: failed to click."),
      {});
}

}  // namespace ai_chat

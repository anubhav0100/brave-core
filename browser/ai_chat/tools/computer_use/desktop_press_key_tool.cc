// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_press_key_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "brave/browser/computer_use/action_risk_classifier.h"
#include "brave/browser/computer_use/input_injector.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyKey[] = "key";
}  // namespace

DesktopPressKeyTool::DesktopPressKeyTool(
    content::BrowserContext* browser_context)
    : DesktopInputToolBase(browser_context) {}

DesktopPressKeyTool::~DesktopPressKeyTool() = default;

std::string_view DesktopPressKeyTool::Name() const {
  return mojom::kDesktopPressKeyToolName;
}

std::string_view DesktopPressKeyTool::Description() const {
  return "Presses a named key or modifier combo (e.g. \"Enter\", "
         "\"Escape\", \"Tab\", \"Backspace\", \"Ctrl+C\", \"Ctrl+A\", "
         "\"Ctrl+Shift+T\") into whatever currently has keyboard focus, "
         "anywhere on the desktop - not just this browser.";
}

std::optional<base::DictValue> DesktopPressKeyTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyKey,
       StringProperty("Key or modifier combo, e.g. \"Enter\" or \"Ctrl+C\"")},
  });
}

std::optional<std::vector<std::string>>
DesktopPressKeyTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyKey});
}

std::optional<std::pair<std::string, std::string>>
DesktopPressKeyTool::GetActionContext(
    const std::string& arguments_json) const {
  auto input = base::JSONReader::ReadDict(arguments_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* key = input ? input->FindString(kPropertyKey) : nullptr;
  if (!key) {
    return std::nullopt;
  }
  return std::make_pair(computer_use::GetForegroundProcessName(), *key);
}

void DesktopPressKeyTool::UseTool(const std::string& input_json,
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
  const std::string* key = input ? input->FindString(kPropertyKey) : nullptr;
  if (!key) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'key'"), {});
    return;
  }

  std::string process_name = computer_use::GetForegroundProcessName();
  bool success = input_injector_->PressKey(*key);
  if (success) {
    MarkAppInteracted(process_name);
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          success ? "Key pressed."
                 : "Error: unrecognized key - could not press '" + *key +
                       "'."),
      {});
}

}  // namespace ai_chat

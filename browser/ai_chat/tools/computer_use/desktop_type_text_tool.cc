// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_type_text_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/computer_use/action_risk_classifier.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/computer_use/input_injector.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyText[] = "text";
}  // namespace

DesktopTypeTextTool::DesktopTypeTextTool(
    content::BrowserContext* browser_context)
    : DesktopInputToolBase(browser_context) {}

DesktopTypeTextTool::~DesktopTypeTextTool() = default;

std::string_view DesktopTypeTextTool::Name() const {
  return mojom::kDesktopTypeTextToolName;
}

std::string_view DesktopTypeTextTool::Description() const {
  return "Types text into whatever currently has keyboard focus, anywhere "
         "on the desktop - not just this browser. Click the target field "
         "first with desktop_click if it doesn't already have focus.";
}

std::optional<base::DictValue> DesktopTypeTextTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyText, StringProperty("The text to type")},
  });
}

std::optional<std::vector<std::string>>
DesktopTypeTextTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyText});
}

std::optional<std::pair<std::string, std::string>>
DesktopTypeTextTool::GetActionContext(
    const std::string& arguments_json) const {
  auto input = base::JSONReader::ReadDict(arguments_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* text = input ? input->FindString(kPropertyText) : nullptr;
  if (!text) {
    return std::nullopt;
  }
  return std::make_pair(GetForegroundTargetProcessName(), *text);
}

void DesktopTypeTextTool::UseTool(const std::string& input_json,
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
  const std::string* text = input ? input->FindString(kPropertyText) : nullptr;
  if (!text) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'text'"), {});
    return;
  }

  std::string process_name = GetForegroundTargetProcessName();
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          browser_context_);
  bool success;
  if (state->IsRdpActive()) {
    std::u16string text16 = base::UTF8ToUTF16(*text);
    for (char16_t ch : text16) {
      if (ch == u'\n') {
        state->SendRdpKeyEvent(VK_RETURN, /*key_down=*/true);
        state->SendRdpKeyEvent(VK_RETURN, /*key_down=*/false);
        continue;
      }
      state->SendRdpCharEvent(ch);
    }
    success = true;
  } else {
    success = input_injector_->TypeText(*text);
  }
  if (success) {
    MarkAppInteracted(process_name);
  }
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Typed."
                                         : "Error: failed to type text."),
      {});
}

}  // namespace ai_chat

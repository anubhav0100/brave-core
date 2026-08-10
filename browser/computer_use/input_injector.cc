// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/input_injector.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <vector>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace ai_chat {

namespace {

// Maps a lowercase, human-readable key name to its Windows virtual-key
// code. Covers the keys an AI is realistically going to name when
// navigating a UI - not exhaustive.
const std::map<std::string, WORD>& NamedKeyMap() {
  static const base::NoDestructor<std::map<std::string, WORD>> kMap({
      {"enter", VK_RETURN},     {"return", VK_RETURN},
      {"tab", VK_TAB},          {"escape", VK_ESCAPE},
      {"esc", VK_ESCAPE},       {"backspace", VK_BACK},
      {"delete", VK_DELETE},    {"del", VK_DELETE},
      {"space", VK_SPACE},      {"up", VK_UP},
      {"arrowup", VK_UP},       {"down", VK_DOWN},
      {"arrowdown", VK_DOWN},   {"left", VK_LEFT},
      {"arrowleft", VK_LEFT},   {"right", VK_RIGHT},
      {"arrowright", VK_RIGHT}, {"home", VK_HOME},
      {"end", VK_END},          {"pageup", VK_PRIOR},
      {"pagedown", VK_NEXT},    {"insert", VK_INSERT},
      {"f1", VK_F1},            {"f2", VK_F2},
      {"f3", VK_F3},            {"f4", VK_F4},
      {"f5", VK_F5},            {"f6", VK_F6},
      {"f7", VK_F7},            {"f8", VK_F8},
      {"f9", VK_F9},            {"f10", VK_F10},
      {"f11", VK_F11},          {"f12", VK_F12},
  });
  return *kMap;
}

const std::map<std::string, WORD>& ModifierKeyMap() {
  static const base::NoDestructor<std::map<std::string, WORD>> kMap({
      {"ctrl", VK_CONTROL}, {"control", VK_CONTROL},
      {"alt", VK_MENU},     {"shift", VK_SHIFT},
      {"win", VK_LWIN},     {"meta", VK_LWIN},
      {"cmd", VK_LWIN},     {"super", VK_LWIN},
  });
  return *kMap;
}

void SendKeyboardInput(uint32_t flags, WORD virtual_key) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = virtual_key;
  input.ki.dwFlags = flags;
  if (SendInput(1, &input, sizeof(INPUT)) == 0) {
    PLOG(ERROR) << "Failed to inject a key event";
  }
}

void SendUnicodeCharInput(uint32_t flags, wchar_t ch) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wScan = ch;
  input.ki.dwFlags = KEYEVENTF_UNICODE | flags;
  if (SendInput(1, &input, sizeof(INPUT)) == 0) {
    PLOG(ERROR) << "Failed to inject a unicode key event";
  }
}

// Converts an absolute desktop pixel coordinate to the normalized
// 0-65535 range MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK expects.
// Returns false if the virtual screen's metrics can't be read.
bool NormalizeCoordinates(int x, int y, LONG* out_dx, LONG* out_dy) {
  int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (width <= 1 || height <= 1) {
    return false;
  }
  int cx = std::clamp(x, 0, width);
  int cy = std::clamp(y, 0, height);
  *out_dx = static_cast<LONG>((cx * 65535) / (width - 1));
  *out_dy = static_cast<LONG>((cy * 65535) / (height - 1));
  return true;
}

void AppendMouseMove(std::vector<INPUT>* inputs, LONG dx, LONG dy) {
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dx = dx;
  input.mi.dy = dy;
  input.mi.dwFlags =
      MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  inputs->push_back(input);
}

void AppendMouseButton(std::vector<INPUT>* inputs,
                       const std::string& button,
                       bool down) {
  INPUT input = {};
  input.type = INPUT_MOUSE;
  if (button == "right") {
    input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
  } else if (button == "middle") {
    input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
  } else {
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  }
  inputs->push_back(input);
}

// Parses e.g. "Ctrl+Shift+T" into modifier VK codes plus a main VK code.
// Returns false if the main key isn't recognized.
bool ParseKeyCombo(const std::string& key,
                   std::vector<WORD>* modifiers,
                   WORD* main_vk) {
  std::vector<std::string> parts = base::SplitString(
      key, "+", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (parts.empty()) {
    return false;
  }

  std::string main_token = base::ToLowerASCII(parts.back());
  parts.pop_back();

  const auto& modifier_map = ModifierKeyMap();
  for (const auto& part : parts) {
    auto it = modifier_map.find(base::ToLowerASCII(part));
    if (it == modifier_map.end()) {
      return false;
    }
    modifiers->push_back(it->second);
  }

  const auto& named_map = NamedKeyMap();
  if (auto it = named_map.find(main_token); it != named_map.end()) {
    *main_vk = it->second;
    return true;
  }

  if (main_token.size() == 1) {
    std::u16string utf16 = base::UTF8ToUTF16(main_token);
    SHORT vk_and_shift = VkKeyScanW(utf16[0]);
    if (vk_and_shift == -1) {
      return false;
    }
    *main_vk = LOBYTE(vk_and_shift);
    BYTE shift_state = HIBYTE(vk_and_shift);
    if ((shift_state & 1) &&
        std::find(modifiers->begin(), modifiers->end(), VK_SHIFT) ==
            modifiers->end()) {
      modifiers->push_back(VK_SHIFT);
    }
    if ((shift_state & 2) &&
        std::find(modifiers->begin(), modifiers->end(), VK_CONTROL) ==
            modifiers->end()) {
      modifiers->push_back(VK_CONTROL);
    }
    if ((shift_state & 4) &&
        std::find(modifiers->begin(), modifiers->end(), VK_MENU) ==
            modifiers->end()) {
      modifiers->push_back(VK_MENU);
    }
    return true;
  }

  return false;
}

}  // namespace

InputInjector::InputInjector() = default;
InputInjector::~InputInjector() = default;

bool InputInjector::MoveMouse(int x, int y) {
  LONG dx, dy;
  if (!NormalizeCoordinates(x, y, &dx, &dy)) {
    return false;
  }
  std::vector<INPUT> inputs;
  AppendMouseMove(&inputs, dx, dy);
  return SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                   sizeof(INPUT)) == inputs.size();
}

bool InputInjector::Click(int x,
                          int y,
                          const std::string& button,
                          bool double_click) {
  LONG dx, dy;
  if (!NormalizeCoordinates(x, y, &dx, &dy)) {
    return false;
  }
  std::vector<INPUT> inputs;
  AppendMouseMove(&inputs, dx, dy);
  AppendMouseButton(&inputs, button, /*down=*/true);
  AppendMouseButton(&inputs, button, /*down=*/false);
  if (double_click) {
    AppendMouseButton(&inputs, button, /*down=*/true);
    AppendMouseButton(&inputs, button, /*down=*/false);
  }
  return SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                   sizeof(INPUT)) == inputs.size();
}

bool InputInjector::Scroll(int x, int y, int delta_x, int delta_y) {
  LONG dx, dy;
  if (!NormalizeCoordinates(x, y, &dx, &dy)) {
    return false;
  }
  std::vector<INPUT> inputs;
  AppendMouseMove(&inputs, dx, dy);
  if (delta_y != 0) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = delta_y;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    inputs.push_back(input);
  }
  if (delta_x != 0) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = delta_x;
    input.mi.dwFlags = MOUSEEVENTF_HWHEEL | MOUSEEVENTF_WHEEL;
    inputs.push_back(input);
  }
  return SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                   sizeof(INPUT)) == inputs.size();
}

bool InputInjector::TypeText(const std::string& utf8_text) {
  std::u16string text = base::UTF8ToUTF16(utf8_text);
  for (char16_t ch : text) {
    if (ch == '\n') {
      SendKeyboardInput(0, VK_RETURN);
      SendKeyboardInput(KEYEVENTF_KEYUP, VK_RETURN);
      continue;
    }
    SendUnicodeCharInput(0, ch);
    SendUnicodeCharInput(KEYEVENTF_KEYUP, ch);
  }
  return true;
}

bool InputInjector::PressKey(const std::string& key) {
  std::vector<WORD> modifiers;
  WORD main_vk = 0;
  if (!ParseKeyCombo(key, &modifiers, &main_vk)) {
    return false;
  }

  for (WORD vk : modifiers) {
    SendKeyboardInput(0, vk);
  }
  SendKeyboardInput(0, main_vk);
  SendKeyboardInput(KEYEVENTF_KEYUP, main_vk);
  for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
    SendKeyboardInput(KEYEVENTF_KEYUP, *it);
  }
  return true;
}

}  // namespace ai_chat

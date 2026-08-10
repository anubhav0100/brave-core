// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/global_stop_hotkey.h"

#include <windows.h>

#include "base/functional/bind.h"

namespace computer_use {

namespace {
constexpr int kStopHotkeyId = 1;
}  // namespace

GlobalStopHotkey::GlobalStopHotkey(base::RepeatingClosure on_triggered)
    : on_triggered_(std::move(on_triggered)) {
  if (!window_.Create(base::BindRepeating(&GlobalStopHotkey::HandleMessage,
                                          base::Unretained(this)))) {
    return;
  }
  registered_ = RegisterHotKey(window_.hwnd(), kStopHotkeyId,
                               MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                               VK_ESCAPE);
}

GlobalStopHotkey::~GlobalStopHotkey() {
  if (registered_) {
    UnregisterHotKey(window_.hwnd(), kStopHotkeyId);
  }
}

bool GlobalStopHotkey::HandleMessage(UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     LRESULT* result) {
  if (message == WM_HOTKEY && wparam == kStopHotkeyId) {
    on_triggered_.Run();
    *result = 0;
    return true;
  }
  return false;
}

}  // namespace computer_use

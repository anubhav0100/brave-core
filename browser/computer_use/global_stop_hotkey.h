// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_GLOBAL_STOP_HOTKEY_H_
#define BRAVE_BROWSER_COMPUTER_USE_GLOBAL_STOP_HOTKEY_H_

#include "base/functional/callback.h"
#include "base/win/message_window.h"

namespace computer_use {

// Registers a system-wide hotkey (Ctrl+Alt+Shift+Esc) that instantly halts
// AI-driven input, even when the browser doesn't have focus - this is the
// whole point, since once the AI is clicking around in other apps the
// browser's own UI may not be reachable. See brave-ai-computer-use.md,
// Phase 2 safety architecture item 2 ("Instant stop").
//
// Registered for the lifetime of the owning ComputerUseSessionState (i.e.
// for as long as the profile is alive), not just while a session is
// active - pressing it when nothing is running is a harmless no-op, and
// that's much simpler than register/unregister lifecycle management.
class GlobalStopHotkey {
 public:
  explicit GlobalStopHotkey(base::RepeatingClosure on_triggered);
  ~GlobalStopHotkey();
  GlobalStopHotkey(const GlobalStopHotkey&) = delete;
  GlobalStopHotkey& operator=(const GlobalStopHotkey&) = delete;

 private:
  bool HandleMessage(UINT message,
                     WPARAM wparam,
                     LPARAM lparam,
                     LRESULT* result);

  base::RepeatingClosure on_triggered_;
  base::win::MessageWindow window_;
  bool registered_ = false;
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_GLOBAL_STOP_HOTKEY_H_

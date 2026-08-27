// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_INPUT_INJECTOR_H_
#define BRAVE_BROWSER_COMPUTER_USE_INPUT_INJECTOR_H_

#include <windows.h>

#include <string>
#include <vector>

namespace ai_chat {

// Parses e.g. "Ctrl+Shift+T" into modifier virtual-key codes plus a main
// virtual-key code - the same key-name table InputInjector::PressKey uses
// via SendInput, exposed so an RDP-active desktop_press_key_tool call can
// share it instead of duplicating the table (see rdp_session.h's
// SendKeyEvent, which needs a VK code directly rather than a key name).
// Returns false if the main key isn't recognized.
bool ParseKeyCombo(const std::string& key,
                   std::vector<WORD>* modifiers,
                   WORD* main_vk);

// Injects synthetic mouse and keyboard input at the OS level via Win32's
// SendInput(), targeting whatever's on screen at the given desktop
// coordinates - not scoped to this browser or any particular window. Logic
// adapted from remoting/host/input_injector_win.cc (Chrome Remote Desktop's
// own input injector - not itself linkable outside remoting/, see
// brave-ai-computer-use.md's "Grounding" section).
//
// Known limitation: coordinates are treated as pixels within
// GetSystemMetrics(SM_CXVIRTUALSCREEN/SM_CYVIRTUALSCREEN), i.e. the same
// simplifying assumption input_injector_win.cc itself makes - on a
// multi-monitor setup where the primary display isn't the leftmost/topmost
// one, the virtual desktop's true origin can be non-zero and this will be
// off by that offset.
class InputInjector {
 public:
  InputInjector();
  ~InputInjector();
  InputInjector(const InputInjector&) = delete;
  InputInjector& operator=(const InputInjector&) = delete;

  bool MoveMouse(int x, int y);

  // `button` is one of "left", "right", "middle". Moves the pointer to
  // (x, y) first, then performs a full down-then-up click (twice, for
  // `double_click`) - all in one SendInput() batch so nothing else can be
  // injected in between.
  bool Click(int x, int y, const std::string& button, bool double_click);

  // Scrolls at (x, y). Positive `delta_y` scrolls up, positive `delta_x`
  // scrolls right (matches the wheel-notch convention: 120 units/notch).
  bool Scroll(int x, int y, int delta_x, int delta_y);

  // Types `utf8_text` via Unicode key injection - works regardless of
  // keyboard layout, but (being layout-independent synthetic Unicode
  // input, not real key presses) can't participate in modifier combos -
  // that's PressKey's job.
  bool TypeText(const std::string& utf8_text);

  // Presses a named key, optionally with modifiers, e.g. "Enter", "Ctrl+C",
  // "Ctrl+Shift+T". Held modifiers are released in reverse order after the
  // main key. Returns false if `key` isn't a recognized key name.
  //
  // Note: a synthetic Ctrl+Alt+Delete cannot and will not invoke the real
  // Secure Attention Sequence - Windows deliberately blocks SendInput from
  // triggering it (that requires the privileged SendSAS() API instead), so
  // this is a safe no-op if ever requested, not a bypass.
  bool PressKey(const std::string& key);
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_COMPUTER_USE_INPUT_INJECTOR_H_

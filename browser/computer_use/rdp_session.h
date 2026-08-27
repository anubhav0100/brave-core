// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_
#define BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/functional/callback.h"

namespace computer_use {

// The RDP session window's Win32 class name (see Impl's DECLARE_WND_CLASS in
// rdp_session.cc) - exported so other code (desktop_capture_session.cc's
// hardware-accelerated-window compositing) can find this specific window by
// class rather than by its human-readable, host-dependent title text.
inline constexpr wchar_t kRdpSessionWindowClassName[] =
    L"BraveComputerUseRdpSession";

// Hosts Microsoft's RDP ActiveX control (MsTscAx) in a real top-level
// window titled "RDP: <host> - AI Automation Browser" - created but never
// shown on screen (see Connect()). True tab embedding (making the control
// itself a child of a browser tab's native view hierarchy) isn't something
// Chromium's tab strip supports - tabs are WebContents, not arbitrary
// native views - so instead this window is hidden and its content is
// captured window-specifically (GetWindowId() + DesktopCaptureSession::
// CaptureWindow()) and streamed into chrome://computer-use's own tab as a
// live image, with mouse/keyboard forwarded back via SendMouseEvent()/
// SendKeyEvent() - the standard approach real remote-desktop web clients
// use. See computer_use_session_state.h for the capture-timer/mojo-push
// wiring built on top of this.
//
// Adapted from remoting/host/win/rdp_client_window.cc's ActiveX-hosting
// pattern (not linked - remoting/'s DEPS forbids external dependents;
// com_imported_mstscax.h, the MIDL-generated COM interface header that
// pattern depends on, is copied into browser/computer_use/win/ for the
// same reason). Deliberately does NOT port that file's window-hiding or
// its WH_CBT hook that auto-dismisses any dialog the RDP control shows -
// this is a real interactive session a human should see, including its
// certificate/trust warnings, not a headless one. Also deliberately never
// touches a password: host/port are the only inputs this class takes:
// authentication is handled by the RDP control's own native prompt (or by
// Windows Credential Manager if the user has already saved credentials for
// that host, exactly as mstsc.exe itself would use them) - the browser
// process's memory never holds an RDP password.
//
// Must be constructed, used, and destroyed from a UI-message-pumped
// thread (COM STA + a real window) - the browser's UI thread qualifies.
class RdpSession {
 public:
  // `success` is false and `error_message` non-empty if the connection
  // attempt itself failed (bad host, refused, auth failed then the user
  // closed the credential prompt, etc.). Fires exactly once, for the
  // outcome of this specific Connect() call - not for later disconnects
  // once already connected (see SetDisconnectedCallback for that).
  using ConnectedCallback =
      base::OnceCallback<void(bool success, std::string error_message)>;

  // Fires whenever an established session ends, for any reason (user
  // closed the window, server disconnected, network error) - lets the
  // owner clear its "RDP active" state regardless of why.
  using DisconnectedCallback =
      base::RepeatingCallback<void(std::string reason)>;

  RdpSession();
  ~RdpSession();
  RdpSession(const RdpSession&) = delete;
  RdpSession& operator=(const RdpSession&) = delete;

  // Tags the session window with `app_user_model_id` (see
  // shell_integration::win::GetAppUserModelIdForBrowser) so Windows groups
  // it in the taskbar/alt-tab with the profile that opened it, instead of
  // defaulting to the browser process's own app identity - which made an
  // RDP session opened from the AI Chat Agent profile appear to belong to a
  // different/default profile. Call before Connect(); a no-op if never
  // called (or called with an empty string).
  void SetAppUserModelId(std::wstring app_user_model_id);

  void Connect(const std::string& host, int port, ConnectedCallback callback);

  // Requests a graceful disconnect; the window closes once done. Safe to
  // call even if not connected yet.
  void Disconnect();

  bool IsConnected() const;

  void SetDisconnectedCallback(DisconnectedCallback callback);

  // An opaque id for this session's (hidden) window, suitable for
  // DesktopCaptureSession::CaptureWindow() - the same value a Windows HWND
  // reinterpret_casts to. 0 if there's no window (never connected, or
  // already torn down).
  intptr_t GetWindowId() const;

  // Posts a mouse event directly to the RDP ActiveX control's window,
  // bypassing SendInput entirely - this works even though the window is
  // hidden, unlike SendInput which requires the target to be the real
  // on-screen focused/hit-tested window. `x`/`y` are client-area
  // coordinates (i.e. relative to the session window's own content, not
  // the desktop) - matching what a window-specific capture (GetWindowId())
  // naturally produces. `buttons` is a bitmask matching DOM
  // MouseEvent.buttons (1=left, 2=right, 4=middle). `wheel_delta` matches
  // the legacy but still-supported DOM WheelEvent.wheelDelta (positive =
  // scroll up/away from the user, in multiples of 120) - deliberately not
  // the standardized WheelEvent.deltaY, which is inverted and not
  // consistently scaled; 0 for a plain move/click with no scrolling.
  void SendMouseEvent(int x, int y, int buttons, int wheel_delta);

  // Posts a key event directly to the RDP ActiveX control's window.
  // `virtual_key_code` is a Windows VK_* code - for common keys this is
  // numerically identical to the deprecated DOM KeyboardEvent.keyCode,
  // which is the simplification this is designed to be fed from directly;
  // exotic/IME input isn't handled specially in this first cut.
  void SendKeyEvent(int virtual_key_code, bool key_down);

  // Posts a single typed character directly to the RDP ActiveX control's
  // window via WM_CHAR - unlike SendKeyEvent, this takes the character
  // itself rather than a virtual-key code, so it can inject arbitrary
  // Unicode text regardless of keyboard layout, the same guarantee
  // InputInjector::TypeText makes for the non-RDP desktop_type_text path.
  void SendCharEvent(char16_t character);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_

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
// window titled "RDP: <host> - AI Automation Browser" - positioned at a
// real, on-screen location but pushed to the bottom of the Z-order (see
// Connect()), and created with WS_EX_TOOLWINDOW, so it never appears in
// the taskbar or Alt+Tab and is normally covered by whatever else is on
// screen (typically the browser window itself), without ever being
// activated or stealing focus. This is the third design tried, after two
// that each broke hardware-accelerated capture specifically: never
// showing the window at all leaves it with no renderable surface, so
// window capture (GDI PrintWindow / the Windows Graphics Capture
// fallback) permanently fails; positioning it entirely outside the
// virtual desktop's bounds captures as solid black, because WGC (window
// capture's fallback for hardware-accelerated surfaces, which this
// content needs) treats a window with zero on-screen presence as fully
// occluded/cloaked and returns blank content, the same power-saving
// behavior a minimized window gets; a fully-transparent layered window
// (WS_EX_LAYERED, alpha 0) ALSO captured as solid black, since WGC
// reflects the window's real alpha-blended visual result. Both of those
// capture mechanisms fundamentally reflect "what a human would actually
// see," so any technique that hides content from a human also hides it
// from them - staying at the bottom of the Z-order on a real, valid
// screen position is what keeps the window genuinely on-screen and
// opaque (so capture gets real content) while still being invisible in
// practice, without relying on a Windows mechanism that conflates
// "invisible to a human" with "invisible to capture." True tab embedding
// (making the control itself a child of a browser tab's native view
// hierarchy) isn't something Chromium's tab strip supports - tabs are
// WebContents, not arbitrary native views - so instead this window's
// content is captured window-specifically (GetWindowId() +
// DesktopCaptureSession::CaptureWindow()) and streamed into
// chrome://computer-use's own tab as a live image, with mouse/keyboard
// forwarded back via SendMouseEvent()/SendKeyEvent() - the standard
// approach real remote-desktop web clients use. See
// computer_use_session_state.h for the capture-timer/mojo-push wiring
// built on top of this.
//
// Adapted from remoting/host/win/rdp_client_window.cc's ActiveX-hosting
// pattern (not linked - remoting/'s DEPS forbids external dependents;
// com_imported_mstscax.h, the MIDL-generated COM interface header that
// pattern depends on, is copied into browser/computer_use/win/ for the
// same reason). Deliberately does NOT port that file's WH_CBT hook that
// auto-dismisses any dialog the RDP control shows - this is a real
// interactive session a human should see, including its certificate/trust
// warnings, not a headless one. Also deliberately never touches a
// password: host/port are the only inputs this class takes:
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

  // Re-asserts that the session window stays at the bottom of the
  // Z-order (see Connect()'s doc comment) - the RDP ActiveX control
  // brings its own window to the foreground once a session actually
  // finishes connecting, undoing the placement Connect() establishes at
  // creation time. Called once automatically when the connection
  // completes; ComputerUseSessionState also calls this on every capture
  // timer tick for the rest of the session as a defensive measure against
  // other events doing the same. A no-op if there's no window.
  void KeepBelowOtherWindows();

  // Shows (or re-hides) the session window as a real, focusable, on-screen
  // window - an explicit escape hatch for the user to get guaranteed-
  // working mouse/keyboard control, on top of (not instead of) the
  // embedded canvas view: window capture keeps running normally either
  // way, so the canvas stays live. `show=true` brings the window to the
  // foreground normally, exactly like a real window the user opened
  // directly; `show=false` returns to KeepBelowOtherWindows()'s hidden-
  // in-practice placement. A no-op if there's no window.
  void SetShownAsWindow(bool show);

  // Registers a callback fired when the user clicks the small "AI
  // Assistant" button shown in the corner of the session window while
  // SetShownAsWindow(true) is in effect - the popup covers the browser
  // window underneath it (including its AI Assistant side panel), so this
  // is the only way to get back to it without hunting for the browser
  // window separately. The button itself only exists on the session
  // window (hidden along with everything else while shown_as_window_ is
  // false), so this never affects what the embedded canvas view captures.
  void SetOpenAiAssistantCallback(base::RepeatingClosure callback);

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

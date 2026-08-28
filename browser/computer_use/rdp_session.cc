// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/rdp_session.h"

// clang-format off
// This needs to be included before ATL headers, matching
// remoting/host/win/rdp_client_window.h's own ordering requirement.
#include "base/win/atl.h"
// clang-format on

#include <atlapp.h>
#include <atlcrack.h>
#include <wrl/client.h>

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_bstr.h"
#include "brave/browser/computer_use/win/com_imported_mstscax.h"
#include "ui/base/win/atl_module.h"
#include "ui/base/win/shell.h"

namespace computer_use {

namespace {

// Posted to the session window right after WM_SETFOCUS is posted to the
// ActiveX control (see Impl::NotifyFocused()), to re-hide the window
// immediately if that focus notification made the control show itself -
// see NotifyFocused()'s own comment for why this two-message sequence,
// rather than the repeating capture timer alone, is needed.
constexpr UINT kReassertHiddenMessage = WM_APP + 1;

// Child control id for the "AI Assistant" button shown in the corner of
// the session window while it's shown as a real popup (see
// Impl::SetShownAsWindow) - lets the user get back to the browser's AI
// Assistant panel without needing to find the (likely now-covered)
// browser window on their own.
constexpr int kOpenAiAssistantButtonId = 1001;
constexpr int kOpenAiAssistantButtonWidth = 130;
constexpr int kOpenAiAssistantButtonHeight = 28;
constexpr int kOpenAiAssistantButtonMargin = 8;

// RDP session disconnect reason codes that aren't real errors - matches
// remoting/host/win/rdp_client_window.cc's own classification.
constexpr long kDisconnectReasonNoInfo = 0;
constexpr long kDisconnectReasonLocalNotError = 1;
constexpr long kDisconnectReasonRemoteByUser = 2;
constexpr long kDisconnectReasonByServer = 3;

// The RDP ActiveX control's put_Server() wants a bare hostname/IP and
// rejects anything else with E_INVALIDARG (hresult=0x80070057) - a "host:
// port" string typed into a single field (an easy, common thing to type,
// especially since that's also what the RDP permission challenge's own
// plan text and mstsc.exe's own address bar both accept) is exactly such a
// rejection. Rather than surfacing that as an opaque HRESULT, split a
// trailing ":<digits>" off the host here and use it as the port instead -
// it's the only reasonable interpretation of that input, and matches what
// the user almost certainly meant. Left alone (including IPv6 literals,
// which use colons for a different reason) if the suffix after the last
// ':' isn't purely digits.
void NormalizeHostAndPort(std::string* host, int* port) {
  base::TrimWhitespaceASCII(*host, base::TRIM_ALL, host);
  size_t colon_pos = host->rfind(':');
  if (colon_pos == std::string::npos || colon_pos == host->size() - 1) {
    return;
  }
  std::string_view port_suffix(*host);
  port_suffix.remove_prefix(colon_pos + 1);
  int parsed_port = 0;
  if (!base::StringToInt(port_suffix, &parsed_port) || parsed_port <= 0 ||
      parsed_port > 65535) {
    return;
  }
  std::string_view host_prefix(*host);
  host_prefix.remove_suffix(host->size() - colon_pos);
  if (host_prefix.empty()) {
    return;
  }
  *port = parsed_port;
  *host = std::string(host_prefix);
}

}  // namespace

class RdpSession::Impl
    : public CWindowImpl<Impl, CWindow, CFrameWinTraits>,
      public IDispEventImpl<1,
                            Impl,
                            &__uuidof(mstsc::IMsTscAxEvents),
                            &__uuidof(mstsc::__MSTSCLib),
                            1,
                            0> {
 public:
  DECLARE_WND_CLASS(kRdpSessionWindowClassName)

  Impl() { ui::win::CreateATLModuleIfNeeded(); }

  void SetAppUserModelId(std::wstring app_user_model_id) {
    app_user_model_id_ = std::move(app_user_model_id);
  }

  ~Impl() override {
    if (m_hWnd) {
      DestroyWindow();
    }
  }

  void Connect(const std::string& host,
              int port,
              RdpSession::ConnectedCallback callback) {
    host_ = host;
    port_ = port;
    NormalizeHostAndPort(&host_, &port_);
    connected_callback_ = std::move(callback);

    int width = GetSystemMetrics(SM_CXSCREEN) * 3 / 4;
    int height = GetSystemMetrics(SM_CYSCREEN) * 3 / 4;
    // Positioned at a real, on-screen location, at the bottom of the
    // Z-order rather than made transparent or moved off-screen. Two
    // earlier techniques were tried and both failed for hardware-
    // accelerated content specifically (the RDP control's video decode/
    // present path): positioning the window entirely outside the virtual
    // desktop's bounds captures as solid black (Windows Graphics Capture,
    // window capture's fallback for hardware-accelerated surfaces, treats
    // a window with zero on-screen presence as fully occluded/cloaked and
    // returns blank content - the same power-saving behavior a minimized
    // window gets); a fully-transparent layered window (WS_EX_LAYERED,
    // alpha 0) ALSO captures as solid black, since WGC reflects the
    // window's actual alpha-blended visual result, which is invisible by
    // definition. Both capture mechanisms fundamentally reflect "what a
    // human would actually see," so hiding content from a human hides it
    // from these same capture APIs. Staying at the bottom of the Z-order
    // on a real, valid screen position keeps the window genuinely
    // on-screen and opaque (so WGC/PrintWindow get real content), while
    // in practice being covered by whatever else is on screen - normally
    // the browser window itself. WS_EX_TOOLWINDOW keeps it out of the
    // taskbar/Alt+Tab.
    RECT rect = {0, 0, width, height};
    std::wstring title = base::UTF8ToWide(
        base::StrCat({"RDP: ", host_, " - AI Automation Browser"}));
    if (!Create(nullptr, rect, title.c_str(), 0, WS_EX_TOOLWINDOW) ||
       !m_hWnd) {
      NotifyConnectResult(false, "Failed to create the RDP session window.");
      return;
    }
    ShowWindow(SW_SHOWNOACTIVATE);
    KeepBelowOtherWindows();
  }

  // Re-asserts the bottom-of-Z-order placement Connect() establishes -
  // needed because the RDP ActiveX control brings its own window to the
  // foreground once a session actually finishes connecting (mirroring
  // mstsc.exe's own normal behavior of surfacing itself to the user once
  // ready), undoing the one-time placement from window creation. Called
  // again from OnConnected() for that reason, and from
  // ComputerUseSessionState's repeating capture timer for the rest of the
  // session as a defensive measure, since other events (e.g. a
  // certificate warning dialog closing) could plausibly do the same. A
  // no-op while SetShownAsWindow(true) is in effect - otherwise the timer
  // driving this (every ~200ms) would undo an explicit "show as window"
  // request within a single tick, before the user could ever see it.
  void KeepBelowOtherWindows() {
    if (!m_hWnd || shown_as_window_) {
      return;
    }
    ::SetWindowPos(m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  void SetShownAsWindow(bool show) {
    if (!m_hWnd) {
      return;
    }
    shown_as_window_ = show;
    if (show) {
      ::ShowWindow(m_hWnd, SW_SHOW);
      ::SetForegroundWindow(m_hWnd);
    } else {
      ::SetWindowPos(m_hWnd, HWND_BOTTOM, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Only visible while shown as a real popup - it must never show up in
    // the embedded canvas view's window-specific capture, which targets
    // this exact window regardless of shown_as_window_.
    if (open_ai_assistant_button_) {
      ::ShowWindow(open_ai_assistant_button_, show ? SW_SHOW : SW_HIDE);
    }
  }

  void SetOpenAiAssistantCallback(base::RepeatingClosure callback) {
    open_ai_assistant_callback_ = std::move(callback);
  }

  void Disconnect() {
    if (m_hWnd) {
      SendMessage(WM_CLOSE);
    }
  }

  bool IsConnected() const { return connected_; }

  void SetDisconnectedCallback(RdpSession::DisconnectedCallback callback) {
    disconnected_callback_ = std::move(callback);
  }

  intptr_t GetWindowId() const {
    return reinterpret_cast<intptr_t>(static_cast<HWND>(m_hWnd));
  }

  void SendMouseEvent(int x, int y, int buttons, int wheel_delta) {
    if (!activex_window_.m_hWnd) {
      return;
    }
    HWND target = activex_window_.m_hWnd;
    // `x`/`y` are relative to the captured image, which is this session's
    // top-level window's full outer rect (title bar and borders included -
    // that's what GetWindowId()'s window-specific capture grabs), not its
    // client area where `target` actually lives and interprets message
    // coordinates. Translate by the offset between the two so a click that
    // looks correct on the captured frame actually lands on the same spot
    // in the real session, instead of being off by the title bar's height.
    RECT window_rect{};
    ::GetWindowRect(m_hWnd, &window_rect);
    POINT client_origin{0, 0};
    ::ClientToScreen(m_hWnd, &client_origin);
    x -= (client_origin.x - window_rect.left);
    y -= (client_origin.y - window_rect.top);
    LPARAM position = MAKELPARAM(x, y);
    WPARAM key_state = ((buttons & kMouseButtonLeft) ? MK_LBUTTON : 0) |
                       ((buttons & kMouseButtonRight) ? MK_RBUTTON : 0) |
                       ((buttons & kMouseButtonMiddle) ? MK_MBUTTON : 0);

    ::PostMessage(target, WM_MOUSEMOVE, key_state, position);

    int changed = buttons ^ last_mouse_buttons_;
    if (changed & kMouseButtonLeft) {
      if (buttons & kMouseButtonLeft) {
        NotifyFocused();
      }
      ::PostMessage(target,
                    (buttons & kMouseButtonLeft) ? WM_LBUTTONDOWN
                                                 : WM_LBUTTONUP,
                    key_state, position);
    }
    if (changed & kMouseButtonRight) {
      ::PostMessage(target,
                    (buttons & kMouseButtonRight) ? WM_RBUTTONDOWN
                                                  : WM_RBUTTONUP,
                    key_state, position);
    }
    if (changed & kMouseButtonMiddle) {
      ::PostMessage(target,
                    (buttons & kMouseButtonMiddle) ? WM_MBUTTONDOWN
                                                   : WM_MBUTTONUP,
                    key_state, position);
    }
    last_mouse_buttons_ = buttons;

    if (wheel_delta != 0) {
      // WM_MOUSEWHEEL is the one mouse message whose lparam is defined as
      // SCREEN (not client) coordinates - a Win32 quirk, not a mistake here.
      // `wheel_delta` is already in WM_MOUSEWHEEL's own units/sign
      // convention (multiples of 120, positive = away from the user) - see
      // this method's header comment on why the caller sends
      // WheelEvent.wheelDelta rather than .deltaY.
      POINT screen_point{x, y};
      ::ClientToScreen(target, &screen_point);
      ::PostMessage(target, WM_MOUSEWHEEL,
                    MAKEWPARAM(key_state, static_cast<short>(wheel_delta)),
                    MAKELPARAM(screen_point.x, screen_point.y));
    }

    // Posted last, after every message this call queued above - not just
    // after NotifyFocused()'s WM_SETFOCUS. A button-down message itself
    // (not only the preceding focus notification) was observed to also
    // make the control raise its own top-level window - keyboard input
    // stayed hidden fine (SendKeyEvent's NotifyFocused() call is the only
    // thing that can trigger it there), but a click's WM_LBUTTONDOWN,
    // posted after NotifyFocused()'s own reassert message, had nothing
    // left queued afterward to undo the button-down's own foreground-seek.
    // Queuing this again here, after all of this call's messages, closes
    // that gap regardless of which specific message triggers it.
    if (m_hWnd) {
      ::PostMessage(m_hWnd, kReassertHiddenMessage, 0, 0);
    }
  }

  void SendKeyEvent(int virtual_key_code, bool key_down) {
    if (!activex_window_.m_hWnd) {
      return;
    }
    if (key_down) {
      NotifyFocused();
    }
    ::PostMessage(activex_window_.m_hWnd, key_down ? WM_KEYDOWN : WM_KEYUP,
                 static_cast<WPARAM>(virtual_key_code), 0);
  }

  // Posts WM_SETFOCUS directly to the ActiveX control's window, rather
  // than calling the real SetFocus() API - the control tracks focus
  // itself and was observed to silently ignore posted mouse/keyboard
  // input entirely without this. The real SetFocus() API would work too,
  // but also activates this window's top-level parent as a side effect
  // (per its own documentation), which would undo Connect()'s bottom-of-
  // Z-order placement.
  //
  // Confirmed via testing that even this posted-message-only notification
  // is enough to make the control bring its own top-level window to the
  // foreground anyway (its own internal reaction to gaining focus,
  // independent of how that focus was signaled) - the repeating capture
  // timer's KeepBelowOtherWindows() call (every ~200ms) reliably fixes
  // this eventually, but not fast enough to stop a visible flash/flicker
  // on every single click or keypress. Posting kReassertHiddenMessage to
  // this window immediately after WM_SETFOCUS closes that gap: Windows
  // delivers posted messages to a single thread's queue in the order
  // they were posted regardless of which window each is addressed to, so
  // this window processes the re-hide right after the control finishes
  // reacting to the focus notification, within the same message-pump
  // cycle rather than up to a timer tick later.
  void NotifyFocused() {
    if (!activex_window_.m_hWnd) {
      return;
    }
    ::PostMessage(activex_window_.m_hWnd, WM_SETFOCUS, 0, 0);
    if (m_hWnd) {
      ::PostMessage(m_hWnd, kReassertHiddenMessage, 0, 0);
    }
  }

  void SendCharEvent(char16_t character) {
    if (!activex_window_.m_hWnd) {
      return;
    }
    // WM_CHAR takes the character itself, not a virtual-key code - lets
    // arbitrary Unicode text reach the session regardless of keyboard
    // layout, mirroring what InputInjector::TypeText achieves via
    // SendInput's KEYEVENTF_UNICODE flag.
    ::PostMessage(activex_window_.m_hWnd, WM_CHAR,
                 static_cast<WPARAM>(character), 0);
  }

 private:
  // Bitmask values matching DOM MouseEvent.buttons, which is what this
  // class's SendMouseEvent() is designed to be fed from directly.
  static constexpr int kMouseButtonLeft = 1;
  static constexpr int kMouseButtonRight = 2;
  static constexpr int kMouseButtonMiddle = 4;

  typedef IDispEventImpl<1,
                         Impl,
                         &__uuidof(mstsc::IMsTscAxEvents),
                         &__uuidof(mstsc::__MSTSCLib),
                         1,
                         0>
      RdpEventsSink;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif
  BEGIN_MSG_MAP_EX(Impl)
    MSG_WM_CLOSE(OnClose)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MESSAGE_HANDLER_EX(kReassertHiddenMessage, OnReassertHiddenMessage)
    COMMAND_ID_HANDLER_EX(kOpenAiAssistantButtonId, OnOpenAiAssistantClicked)
  END_MSG_MAP()
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  void OnClose() {
    if (!client_) {
      NotifyDisconnected("closed");
      DestroyWindow();
      return;
    }

    mstsc::ControlCloseStatus close_status;
    HRESULT result = client_->RequestClose(&close_status);
    if (FAILED(result) || close_status != mstsc::controlCloseWaitForEvents) {
      NotifyDisconnected("closed");
      DestroyWindow();
      return;
    }
    // Otherwise expect IMsTscAxEvents::OnDisconnected() to fire, which
    // tears the window down.
  }

  LRESULT OnCreate(CREATESTRUCT* create_struct) {
    RECT rect;
    GetClientRect(&rect);

    if (!app_user_model_id_.empty()) {
      ui::win::SetAppIdForWindow(app_user_model_id_, m_hWnd);
    }

    activex_window_.Create(m_hWnd, rect, nullptr,
                           WS_CHILD | WS_VISIBLE);
    if (activex_window_.m_hWnd == nullptr) {
      NotifyConnectResult(false, "Failed to create the RDP ActiveX host window.");
      return -1;
    }

    // Created after (so stacked above) activex_window_, in the same
    // top-right corner PositionOpenAiAssistantButton() keeps it in on every
    // resize. Starts hidden - SetShownAsWindow() is the only thing that
    // shows it, since it must never appear in the embedded canvas view's
    // capture of this same window.
    open_ai_assistant_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"AI Assistant",
        WS_CHILD | BS_PUSHBUTTON, 0, 0, kOpenAiAssistantButtonWidth,
        kOpenAiAssistantButtonHeight, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenAiAssistantButtonId)),
        _AtlBaseModule.GetModuleInstance(), nullptr);
    if (open_ai_assistant_button_) {
      ::SendMessage(open_ai_assistant_button_, WM_SETFONT,
                   reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),
                   TRUE);
      PositionOpenAiAssistantButton();
    }

    Microsoft::WRL::ComPtr<IUnknown> control;
    HRESULT result = activex_window_.CreateControlEx(
        OLESTR("MsTscAx.MsTscAx"), nullptr, nullptr, &control,
        __uuidof(mstsc::IMsTscAxEvents),
        reinterpret_cast<IUnknown*>(static_cast<RdpEventsSink*>(this)));
    if (FAILED(result)) {
      return LogCreateFailure("Failed to create the RDP ActiveX control",
                              result);
    }

    result = control.As(&client_);
    if (FAILED(result)) {
      return LogCreateFailure("RDP control doesn't implement IMsRdpClient9",
                              result);
    }

    base::win::ScopedBstr server_name(base::UTF8ToWide(host_));
    result = client_->put_Server(server_name.Get());
    if (FAILED(result)) {
      return LogCreateFailure("Failed to set the RDP server", result);
    }

    result = client_->put_ColorDepth(32);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to set color depth", result);
    }
    result = client_->put_DesktopWidth(rect.right - rect.left);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to set desktop width", result);
    }
    result = client_->put_DesktopHeight(rect.bottom - rect.top);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to set desktop height", result);
    }

    Microsoft::WRL::ComPtr<mstsc::IMsRdpClientAdvancedSettings> settings;
    result = client_->get_AdvancedSettings2(&settings);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to get advanced settings", result);
    }
    result = settings->put_RDPPort(port_);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to set the RDP port", result);
    }
    // The control's rendered content otherwise stays pinned to the
    // DesktopWidth/DesktopHeight negotiated above and doesn't follow this
    // window if it's later resized (e.g. the "Open in Window" popup, which
    // is a normal resizable frame) - it just letterboxes the fixed-
    // resolution remote content inside whatever the new size is, leaving
    // gray borders around it. SmartSizing makes the control scale its
    // output to fill its host window at all times instead.
    result = settings->put_SmartSizing(VARIANT_TRUE);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to enable smart sizing", result);
    }
    // Disable drive/printer/port redirection by default - this is a real
    // remote host, not the loopback connection rdp_client_window.cc was
    // written for, so exposing local resources to it isn't something an
    // AI-initiated connection should opt into silently.
    result = settings->put_DisableRdpdr(TRUE);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to disable device redirection", result);
    }

    // Without this, the control can't complete CredSSP/NLA - which every
    // RDP server since Windows 8 / Server 2012 requires by default - so the
    // connection is rejected during the authentication handshake itself,
    // before the control's own native credential dialog (see this class's
    // header comment on never touching a password directly) ever gets a
    // chance to render. That produced exactly this symptom: no prompt, an
    // immediate disconnect.
    Microsoft::WRL::ComPtr<mstsc::IMsRdpClientAdvancedSettings6> settings6;
    result = client_->get_AdvancedSettings7(&settings6);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to get CredSSP settings", result);
    }
    result = settings6->put_EnableCredSspSupport(VARIANT_TRUE);
    if (FAILED(result)) {
      return LogCreateFailure("Failed to enable CredSSP/NLA support", result);
    }

    result = client_->Connect();
    if (FAILED(result)) {
      return LogCreateFailure("Failed to initiate the RDP connection", result);
    }

    return 0;
  }

  void OnDestroy() {
    client_.Reset();
    activex_window_ = CAxWindow2();
    open_ai_assistant_button_ = nullptr;
  }

  void OnSize(UINT type, CSize size) {
    if (activex_window_.m_hWnd) {
      ::MoveWindow(activex_window_.m_hWnd, 0, 0, size.cx, size.cy, TRUE);
    }
    PositionOpenAiAssistantButton();
  }

  LRESULT OnReassertHiddenMessage(UINT, WPARAM, LPARAM) {
    KeepBelowOtherWindows();
    return 0;
  }

  void OnOpenAiAssistantClicked(UINT, int, HWND) {
    if (open_ai_assistant_callback_) {
      open_ai_assistant_callback_.Run();
    }
  }

  // Keeps the button pinned to the top-right corner regardless of the
  // window's current size - relevant since SetShownAsWindow()'s popup is a
  // normal resizable frame (see the SmartSizing comment in OnCreate() on
  // why the remote content itself follows resizes too).
  void PositionOpenAiAssistantButton() {
    if (!open_ai_assistant_button_) {
      return;
    }
    RECT client_rect{};
    GetClientRect(&client_rect);
    int x = (client_rect.right - client_rect.left) -
            kOpenAiAssistantButtonWidth - kOpenAiAssistantButtonMargin;
    ::SetWindowPos(open_ai_assistant_button_, HWND_TOP,
                  std::max(x, kOpenAiAssistantButtonMargin),
                  kOpenAiAssistantButtonMargin, kOpenAiAssistantButtonWidth,
                  kOpenAiAssistantButtonHeight, SWP_NOACTIVATE);
  }

  BEGIN_SINK_MAP(Impl)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 2, &Impl::OnConnected)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 4, &Impl::OnRdpDisconnected)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 10, &Impl::OnFatalError)
  END_SINK_MAP()

  STDMETHOD(OnConnected)() {
    connected_ = true;
    KeepBelowOtherWindows();
    NotifyConnectResult(true, "");
    return S_OK;
  }

  STDMETHOD(OnRdpDisconnected)(long reason) {
    bool was_connected = connected_;
    connected_ = false;
    std::string reason_string = DescribeDisconnectReason(reason);
    if (!was_connected) {
      NotifyConnectResult(false, reason_string);
    }
    NotifyDisconnected(reason_string);
    // Post the destroy: the ActiveX control expects its window to survive
    // until the current message finishes processing (matches
    // rdp_client_window.cc's own guidance on this).
    PostMessage(WM_CLOSE);
    return S_OK;
  }

  STDMETHOD(OnFatalError)(long error_code) {
    std::string message =
        base::StrCat({"fatal RDP error (code=", std::to_string(error_code), ")"});
    bool was_connected = connected_;
    connected_ = false;
    if (!was_connected) {
      NotifyConnectResult(false, message);
    }
    NotifyDisconnected(message);
    return S_OK;
  }

  // Turns a raw OnDisconnected() reason code into readable text. The small
  // set of "not really an error" codes get friendly text directly; anything
  // else asks the control itself for a real description via
  // GetErrorDescription(reason, extendedReason) - the same API Microsoft's
  // own MSTSC ActiveX documentation points at for this - rather than
  // showing the user a bare number they have no way to act on (this is
  // what previously showed "disconnected (reason=2825)" with nothing
  // actionable in it).
  std::string DescribeDisconnectReason(long reason) {
    switch (reason) {
      case kDisconnectReasonNoInfo:
        return "disconnected";
      case kDisconnectReasonLocalNotError:
        return "closed locally";
      case kDisconnectReasonRemoteByUser:
        return "closed by the remote user";
      case kDisconnectReasonByServer:
        return "closed by the server";
      default:
        break;
    }

    if (client_) {
      mstsc::ExtendedDisconnectReasonCode extended_reason =
          static_cast<mstsc::ExtendedDisconnectReasonCode>(0);
      client_->get_ExtendedDisconnectReason(&extended_reason);
      base::win::ScopedBstr description;
      HRESULT result = client_->GetErrorDescription(
          static_cast<unsigned int>(reason),
          static_cast<unsigned int>(extended_reason), description.Receive());
      if (SUCCEEDED(result) && description.Get() && description.Length() > 0) {
        return base::StrCat(
            {base::WideToUTF8(std::wstring(description.Get(),
                                           description.Length())),
             " (reason=", std::to_string(reason), ")"});
      }
    }

    return base::StrCat(
        {"disconnected (reason=", std::to_string(reason), ")"});
  }

  int LogCreateFailure(const char* what, HRESULT result) {
    NotifyConnectResult(
        false, base::StrCat({what, " (hresult=0x",
                             base::StringPrintf("%08lX", result), ")"}));
    client_.Reset();
    return -1;
  }

  void NotifyConnectResult(bool success, const std::string& error) {
    if (connected_callback_) {
      std::move(connected_callback_).Run(success, error);
    }
  }

  void NotifyDisconnected(const std::string& reason) {
    if (disconnected_callback_) {
      disconnected_callback_.Run(reason);
    }
  }

  std::string host_;
  int port_ = 3389;
  bool connected_ = false;
  std::wstring app_user_model_id_;
  int last_mouse_buttons_ = 0;
  bool shown_as_window_ = false;
  HWND open_ai_assistant_button_ = nullptr;
  base::RepeatingClosure open_ai_assistant_callback_;

  RdpSession::ConnectedCallback connected_callback_;
  RdpSession::DisconnectedCallback disconnected_callback_;

  CAxWindow2 activex_window_;
  Microsoft::WRL::ComPtr<mstsc::IMsRdpClient9> client_;
};

RdpSession::RdpSession() : impl_(std::make_unique<Impl>()) {}

RdpSession::~RdpSession() = default;

void RdpSession::SetAppUserModelId(std::wstring app_user_model_id) {
  impl_->SetAppUserModelId(std::move(app_user_model_id));
}

void RdpSession::Connect(const std::string& host,
                         int port,
                         ConnectedCallback callback) {
  impl_->Connect(host, port, std::move(callback));
}

void RdpSession::Disconnect() {
  impl_->Disconnect();
}

bool RdpSession::IsConnected() const {
  return impl_->IsConnected();
}

void RdpSession::SetDisconnectedCallback(DisconnectedCallback callback) {
  impl_->SetDisconnectedCallback(std::move(callback));
}

intptr_t RdpSession::GetWindowId() const {
  return impl_->GetWindowId();
}

void RdpSession::KeepBelowOtherWindows() {
  impl_->KeepBelowOtherWindows();
}

void RdpSession::SetShownAsWindow(bool show) {
  impl_->SetShownAsWindow(show);
}

void RdpSession::SetOpenAiAssistantCallback(base::RepeatingClosure callback) {
  impl_->SetOpenAiAssistantCallback(std::move(callback));
}

void RdpSession::SendMouseEvent(int x, int y, int buttons, int wheel_delta) {
  impl_->SendMouseEvent(x, y, buttons, wheel_delta);
}

void RdpSession::SendKeyEvent(int virtual_key_code, bool key_down) {
  impl_->SendKeyEvent(virtual_key_code, key_down);
}

void RdpSession::SendCharEvent(char16_t character) {
  impl_->SendCharEvent(character);
}

}  // namespace computer_use

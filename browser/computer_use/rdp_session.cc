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

    RECT rect = {0, 0, GetSystemMetrics(SM_CXSCREEN) * 3 / 4,
                GetSystemMetrics(SM_CYSCREEN) * 3 / 4};
    std::wstring title = base::UTF8ToWide(
        base::StrCat({"RDP: ", host_, " - AI Automation Browser"}));
    if (!Create(nullptr, rect, title.c_str()) || !m_hWnd) {
      NotifyConnectResult(false, "Failed to create the RDP session window.");
      return;
    }
    ShowWindow(SW_SHOW);
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

 private:
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
  }

  void OnSize(UINT type, CSize size) {
    if (activex_window_.m_hWnd) {
      ::MoveWindow(activex_window_.m_hWnd, 0, 0, size.cx, size.cy, TRUE);
    }
  }

  BEGIN_SINK_MAP(Impl)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 2, &Impl::OnConnected)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 4, &Impl::OnRdpDisconnected)
  SINK_ENTRY_EX(1, __uuidof(mstsc::IMsTscAxEvents), 10, &Impl::OnFatalError)
  END_SINK_MAP()

  STDMETHOD(OnConnected)() {
    connected_ = true;
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

}  // namespace computer_use

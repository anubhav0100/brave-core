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
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_bstr.h"
#include "brave/browser/computer_use/win/com_imported_mstscax.h"
#include "ui/base/win/atl_module.h"

namespace computer_use {

namespace {

// RDP session disconnect reason codes that aren't real errors - matches
// remoting/host/win/rdp_client_window.cc's own classification.
constexpr long kDisconnectReasonNoInfo = 0;
constexpr long kDisconnectReasonLocalNotError = 1;
constexpr long kDisconnectReasonRemoteByUser = 2;
constexpr long kDisconnectReasonByServer = 3;

std::string DisconnectReasonToString(long reason) {
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
      return base::StrCat({"disconnected (reason=", std::to_string(reason),
                           ")"});
  }
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
  DECLARE_WND_CLASS(L"BraveComputerUseRdpSession")

  Impl() { ui::win::CreateATLModuleIfNeeded(); }

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
    connected_callback_ = std::move(callback);

    RECT rect = {0, 0, GetSystemMetrics(SM_CXSCREEN) * 3 / 4,
                GetSystemMetrics(SM_CYSCREEN) * 3 / 4};
    std::wstring title =
        base::UTF8ToWide(base::StrCat({"RDP: ", host, " - AI Automation Browser"}));
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
    std::string reason_string = DisconnectReasonToString(reason);
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

  RdpSession::ConnectedCallback connected_callback_;
  RdpSession::DisconnectedCallback disconnected_callback_;

  CAxWindow2 activex_window_;
  Microsoft::WRL::ComPtr<mstsc::IMsRdpClient9> client_;
};

RdpSession::RdpSession() : impl_(std::make_unique<Impl>()) {}

RdpSession::~RdpSession() = default;

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

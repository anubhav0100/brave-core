// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "brave/components/computer_use/common/computer_use_ui.mojom.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class WebUI;
}  // namespace content

// The chrome://computer-use page: shows the user, in real time, whatever the
// AI computer-use feature last captured, plus the always-visible "AI is
// viewing this desktop" banner and a Stop control - see
// brave-ai-computer-use.md, Phase 1 ("Safety architecture" #1).
class ComputerUseUI : public content::WebUIController,
                      public computer_use::mojom::PageHandler {
 public:
  ComputerUseUI(content::WebUI* web_ui, std::string_view host);
  ~ComputerUseUI() override;
  ComputerUseUI(const ComputerUseUI&) = delete;
  ComputerUseUI& operator=(const ComputerUseUI&) = delete;

  void BindInterface(
      mojo::PendingReceiver<computer_use::mojom::PageHandler> receiver);

 private:
  // computer_use::mojom::PageHandler:
  void GetState(GetStateCallback callback) override;
  void Stop() override;
  void Resume() override;
  void ConnectRdp(const std::string& host,
                  int32_t port,
                  ConnectRdpCallback callback) override;
  void DisconnectRdp() override;
  void GetRdpHistory(GetRdpHistoryCallback callback) override;
  void GetAlwaysAllowDesktopScreenshot(
      GetAlwaysAllowDesktopScreenshotCallback callback) override;
  void SetAlwaysAllowDesktopScreenshot(bool always_allow) override;
  void BindPage(
      mojo::PendingRemote<computer_use::mojom::Page> page) override;
  void SendRdpMouseEvent(int32_t x,
                         int32_t y,
                         int32_t buttons,
                         int32_t wheel_delta) override;
  void SendRdpKeyEvent(int32_t virtual_key_code, bool key_down) override;
  void SetRdpShownAsWindow(bool show) override;
  void OpenNewComputerUseTab() override;

  // Callbacks registered with ComputerUseSessionState so its RDP capture
  // timer/state-change events push through `page_` - see
  // ComputerUseSessionState::SetRdpFrameCapturedCallback/
  // SetRdpStateChangedCallback.
  void OnRdpFrameCaptured(std::string frame_data_url);
  void OnRdpStateChanged(bool rdp_active,
                         std::string rdp_target_host,
                         int rdp_target_port);

  mojo::Receiver<computer_use::mojom::PageHandler> receiver_{this};
  mojo::Remote<computer_use::mojom::Page> page_;
  base::WeakPtrFactory<ComputerUseUI> weak_ptr_factory_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_

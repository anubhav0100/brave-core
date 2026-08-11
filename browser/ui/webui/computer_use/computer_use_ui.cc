// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/computer_use/computer_use_ui.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/computer_use/browser/resources/grit/computer_use_generated_map.h"
#include "build/build_config.h"
#include "components/grit/brave_components_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

ComputerUseUI::ComputerUseUI(content::WebUI* web_ui, std::string_view host)
    : content::WebUIController(web_ui) {
  CreateAndAddWebUIDataSource(web_ui, host, kComputerUseGenerated,
                              IDR_COMPUTER_USE_HTML);
}

ComputerUseUI::~ComputerUseUI() = default;

void ComputerUseUI::BindInterface(
    mojo::PendingReceiver<computer_use::mojom::PageHandler> receiver) {
  if (receiver_.is_bound()) {
    receiver_.reset();
  }
  receiver_.Bind(std::move(receiver));
}

void ComputerUseUI::GetState(GetStateCallback callback) {
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          web_ui()->GetWebContents()->GetBrowserContext());
  bool rdp_active = false;
  std::string rdp_target_host;
  int rdp_target_port = 0;
#if BUILDFLAG(IS_WIN)
  rdp_active = state->IsRdpActive();
  rdp_target_host = state->GetRdpTargetHost();
  rdp_target_port = state->GetRdpTargetPort();
#endif
  std::move(callback).Run(state->IsActive(), state->IsEmergencyStopped(),
                          state->GetLatestFrameDataUrl(), rdp_active,
                          rdp_target_host, rdp_target_port);
}

void ComputerUseUI::Stop() {
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      web_ui()->GetWebContents()->GetBrowserContext())
      ->EmergencyStop();
}

void ComputerUseUI::Resume() {
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      web_ui()->GetWebContents()->GetBrowserContext())
      ->ResumeAfterStop();
}

void ComputerUseUI::ConnectRdp(const std::string& host,
                               int32_t port,
                               ConnectRdpCallback callback) {
#if BUILDFLAG(IS_WIN)
  // ConnectRdpCallback takes `const std::string&`, but
  // ComputerUseSessionState::ConnectRdp's callback takes `std::string` (it's
  // shared with the AI-facing open_rdp_session_tool, which doesn't go
  // through mojo) - adapt between the two.
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      web_ui()->GetWebContents()->GetBrowserContext())
      ->ConnectRdp(host, port,
                  base::BindOnce(
                      [](ConnectRdpCallback callback, bool success,
                         std::string error) {
                        std::move(callback).Run(success, error);
                      },
                      std::move(callback)));
#else
  std::move(callback).Run(
      false, "RDP is only supported on Windows in this browser build.");
#endif
}

void ComputerUseUI::DisconnectRdp() {
#if BUILDFLAG(IS_WIN)
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      web_ui()->GetWebContents()->GetBrowserContext())
      ->DisconnectRdp();
#endif
}

void ComputerUseUI::GetRdpHistory(GetRdpHistoryCallback callback) {
  std::vector<computer_use::mojom::RdpHistoryEntryPtr> history;
#if BUILDFLAG(IS_WIN)
  auto* state =
      computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
          web_ui()->GetWebContents()->GetBrowserContext());
  for (const auto& entry : state->GetRdpHistory()) {
    auto mojo_entry = computer_use::mojom::RdpHistoryEntry::New();
    mojo_entry->host = entry.host;
    mojo_entry->port = entry.port;
    mojo_entry->connected_at = static_cast<double>(
        entry.connected_at.InMillisecondsSinceUnixEpoch());
    if (entry.disconnected_at) {
      mojo_entry->disconnected_at = static_cast<double>(
          entry.disconnected_at->InMillisecondsSinceUnixEpoch());
    }
    history.push_back(std::move(mojo_entry));
  }
#endif
  std::move(callback).Run(std::move(history));
}

WEB_UI_CONTROLLER_TYPE_IMPL(ComputerUseUI)

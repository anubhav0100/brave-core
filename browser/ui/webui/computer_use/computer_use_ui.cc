// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/computer_use/computer_use_ui.h"

#include <utility>

#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/computer_use/browser/resources/grit/computer_use_generated_map.h"
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
  std::move(callback).Run(state->IsActive(), state->IsEmergencyStopped(),
                          state->GetLatestFrameDataUrl());
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

WEB_UI_CONTROLLER_TYPE_IMPL(ComputerUseUI)

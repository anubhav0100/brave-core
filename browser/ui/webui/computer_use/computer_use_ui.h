// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_

#include <string_view>

#include "brave/components/computer_use/common/computer_use_ui.mojom.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"

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

  mojo::Receiver<computer_use::mojom::PageHandler> receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // BRAVE_BROWSER_UI_WEBUI_COMPUTER_USE_COMPUTER_USE_UI_H_

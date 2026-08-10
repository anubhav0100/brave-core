// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_
#define BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

#include <string>

#include "components/keyed_service/core/keyed_service.h"

namespace computer_use {

// Shared, per-profile state for the AI computer-use feature - the most
// recently captured desktop frame and whether a session is active. Written
// by the AI-facing capture tool (GetDesktopScreenshotTool) and read by the
// computer-use WebUI page, so the page can show the user what the AI just
// saw without the two needing a direct reference to each other.
class ComputerUseSessionState : public KeyedService {
 public:
  ComputerUseSessionState();
  ~ComputerUseSessionState() override;
  ComputerUseSessionState(const ComputerUseSessionState&) = delete;
  ComputerUseSessionState& operator=(const ComputerUseSessionState&) = delete;

  void SetLatestFrame(std::string frame_data_url);
  void Stop();

  bool IsActive() const;
  const std::string& GetLatestFrameDataUrl() const;

 private:
  bool active_ = false;
  std::string latest_frame_data_url_;
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/computer_use_session_state.h"

#include <utility>

namespace computer_use {

ComputerUseSessionState::ComputerUseSessionState() = default;
ComputerUseSessionState::~ComputerUseSessionState() = default;

void ComputerUseSessionState::SetLatestFrame(std::string frame_data_url) {
  active_ = true;
  latest_frame_data_url_ = std::move(frame_data_url);
}

void ComputerUseSessionState::Stop() {
  active_ = false;
  latest_frame_data_url_.clear();
}

bool ComputerUseSessionState::IsActive() const {
  return active_;
}

const std::string& ComputerUseSessionState::GetLatestFrameDataUrl() const {
  return latest_frame_data_url_;
}

}  // namespace computer_use

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/computer_use_session_state.h"

#include <utility>

#include "base/functional/bind.h"

#if BUILDFLAG(IS_WIN)
#include "brave/browser/computer_use/global_stop_hotkey.h"
#include "brave/browser/computer_use/rdp_session.h"
#endif

namespace computer_use {

ComputerUseSessionState::ComputerUseSessionState() {
#if BUILDFLAG(IS_WIN)
  global_stop_hotkey_ = std::make_unique<GlobalStopHotkey>(base::BindRepeating(
      &ComputerUseSessionState::EmergencyStop, base::Unretained(this)));
#endif
}

ComputerUseSessionState::~ComputerUseSessionState() = default;

void ComputerUseSessionState::SetLatestFrame(std::string frame_data_url) {
  active_ = true;
  latest_frame_data_url_ = std::move(frame_data_url);
}

bool ComputerUseSessionState::IsActive() const {
  return active_;
}

const std::string& ComputerUseSessionState::GetLatestFrameDataUrl() const {
  return latest_frame_data_url_;
}

void ComputerUseSessionState::GrantInputConsent() {
  input_consent_granted_ = true;
}

bool ComputerUseSessionState::HasInputConsent() const {
  return input_consent_granted_;
}

void ComputerUseSessionState::MarkAppInteracted(
    const std::string& process_name) {
  interacted_apps_.insert(process_name);
}

bool ComputerUseSessionState::HasInteractedWithApp(
    const std::string& process_name) const {
  return interacted_apps_.contains(process_name);
}

void ComputerUseSessionState::EmergencyStop() {
  emergency_stopped_ = true;
  active_ = false;
}

void ComputerUseSessionState::ResumeAfterStop() {
  emergency_stopped_ = false;
}

bool ComputerUseSessionState::IsEmergencyStopped() const {
  return emergency_stopped_;
}

#if BUILDFLAG(IS_WIN)
void ComputerUseSessionState::ConnectRdp(
    const std::string& host,
    int port,
    base::OnceCallback<void(bool, std::string)> callback) {
  rdp_session_ = std::make_unique<RdpSession>();
  rdp_session_->SetDisconnectedCallback(base::BindRepeating(
      &ComputerUseSessionState::OnRdpDisconnected, base::Unretained(this)));
  rdp_target_host_ = host;
  rdp_session_->Connect(
      host, port,
      base::BindOnce(&ComputerUseSessionState::OnRdpConnectResult,
                     base::Unretained(this), std::move(callback)));
}

void ComputerUseSessionState::DisconnectRdp() {
  if (rdp_session_) {
    rdp_session_->Disconnect();
  }
}

bool ComputerUseSessionState::IsRdpActive() const {
  return rdp_active_;
}

const std::string& ComputerUseSessionState::GetRdpTargetHost() const {
  return rdp_target_host_;
}

void ComputerUseSessionState::OnRdpConnectResult(
    base::OnceCallback<void(bool, std::string)> callback,
    bool success,
    std::string error_message) {
  rdp_active_ = success;
  if (!success) {
    rdp_target_host_.clear();
  }
  std::move(callback).Run(success, std::move(error_message));
}

void ComputerUseSessionState::OnRdpDisconnected(std::string reason) {
  rdp_active_ = false;
  rdp_target_host_.clear();
}
#endif

}  // namespace computer_use

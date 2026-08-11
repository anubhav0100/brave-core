// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/computer_use_session_state.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

#if BUILDFLAG(IS_WIN)
#include "brave/browser/computer_use/global_stop_hotkey.h"
#include "brave/browser/computer_use/rdp_session.h"
#endif

namespace computer_use {

namespace {
#if BUILDFLAG(IS_WIN)
constexpr char kRdpHistoryPref[] = "brave.computer_use.rdp_history";
constexpr char kHostKey[] = "host";
constexpr char kPortKey[] = "port";
constexpr char kConnectedAtKey[] = "connected_at";
constexpr char kDisconnectedAtKey[] = "disconnected_at";
// Cap the persisted log so it can't grow unbounded on a profile that opens
// a lot of RDP sessions over its lifetime.
constexpr size_t kMaxRdpHistoryEntries = 200;
#endif
}  // namespace

ComputerUseSessionState::RdpHistoryEntry::RdpHistoryEntry() = default;
ComputerUseSessionState::RdpHistoryEntry::RdpHistoryEntry(
    std::string host,
    int port,
    base::Time connected_at,
    std::optional<base::Time> disconnected_at)
    : host(std::move(host)),
      port(port),
      connected_at(connected_at),
      disconnected_at(disconnected_at) {}
ComputerUseSessionState::RdpHistoryEntry::RdpHistoryEntry(
    const RdpHistoryEntry&) = default;
ComputerUseSessionState::RdpHistoryEntry&
ComputerUseSessionState::RdpHistoryEntry::operator=(const RdpHistoryEntry&) =
    default;
ComputerUseSessionState::RdpHistoryEntry::~RdpHistoryEntry() = default;

ComputerUseSessionState::ComputerUseSessionState(PrefService* prefs) {
#if BUILDFLAG(IS_WIN)
  prefs_ = prefs;
  global_stop_hotkey_ = std::make_unique<GlobalStopHotkey>(base::BindRepeating(
      &ComputerUseSessionState::EmergencyStop, base::Unretained(this)));
#endif
}

ComputerUseSessionState::~ComputerUseSessionState() = default;

// static
void ComputerUseSessionState::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
#if BUILDFLAG(IS_WIN)
  registry->RegisterListPref(kRdpHistoryPref);
#endif
}

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
  rdp_target_port_ = port;
  rdp_session_->Connect(
      host, port,
      base::BindOnce(&ComputerUseSessionState::OnRdpConnectResult,
                     base::Unretained(this), std::move(callback), host,
                     port));
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

int ComputerUseSessionState::GetRdpTargetPort() const {
  return rdp_target_port_;
}

std::vector<ComputerUseSessionState::RdpHistoryEntry>
ComputerUseSessionState::GetRdpHistory() const {
  std::vector<RdpHistoryEntry> history;
  for (const auto& item : prefs_->GetList(kRdpHistoryPref)) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* host = dict->FindString(kHostKey);
    std::optional<int> port = dict->FindInt(kPortKey);
    const base::Value* connected_at_value = dict->Find(kConnectedAtKey);
    if (!host || !port || !connected_at_value) {
      continue;
    }
    std::optional<base::Time> connected_at =
        base::ValueToTime(connected_at_value);
    if (!connected_at) {
      continue;
    }
    std::optional<base::Time> disconnected_at;
    if (const base::Value* disconnected_at_value =
            dict->Find(kDisconnectedAtKey)) {
      disconnected_at = base::ValueToTime(disconnected_at_value);
    }
    history.emplace_back(*host, *port, *connected_at, disconnected_at);
  }
  // Stored oldest-appended-last (new entries are inserted at the front, see
  // AppendRdpHistoryEntry) - already most-recent-first.
  return history;
}

void ComputerUseSessionState::OnRdpConnectResult(
    base::OnceCallback<void(bool, std::string)> callback,
    const std::string& host,
    int port,
    bool success,
    std::string error_message) {
  rdp_active_ = success;
  if (!success) {
    rdp_target_host_.clear();
    rdp_target_port_ = 0;
  } else {
    AppendRdpHistoryEntry(host, port);
  }
  std::move(callback).Run(success, std::move(error_message));
}

void ComputerUseSessionState::OnRdpDisconnected(std::string reason) {
  rdp_active_ = false;
  rdp_target_host_.clear();
  rdp_target_port_ = 0;
  CloseLatestOpenRdpHistoryEntry();
}

void ComputerUseSessionState::AppendRdpHistoryEntry(const std::string& host,
                                                     int port) {
  ScopedListPrefUpdate update(prefs_, kRdpHistoryPref);
  base::DictValue entry;
  entry.Set(kHostKey, host);
  entry.Set(kPortKey, port);
  entry.Set(kConnectedAtKey, base::TimeToValue(base::Time::Now()));
  update->Insert(update->begin(), base::Value(std::move(entry)));
  while (update->size() > kMaxRdpHistoryEntries) {
    update->erase(update->begin() + kMaxRdpHistoryEntries);
  }
}

void ComputerUseSessionState::CloseLatestOpenRdpHistoryEntry() {
  ScopedListPrefUpdate update(prefs_, kRdpHistoryPref);
  for (base::Value& item : *update) {
    base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    if (!dict->Find(kDisconnectedAtKey)) {
      dict->Set(kDisconnectedAtKey, base::TimeToValue(base::Time::Now()));
      return;
    }
  }
}
#endif

}  // namespace computer_use

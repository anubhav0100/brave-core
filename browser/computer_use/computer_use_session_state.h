// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_
#define BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

#include <memory>
#include <string>

#include "base/containers/flat_set.h"
#include "build/build_config.h"
#include "components/keyed_service/core/keyed_service.h"

namespace computer_use {

#if BUILDFLAG(IS_WIN)
class GlobalStopHotkey;
#endif

// Shared, per-profile state for the AI computer-use feature - the most
// recently captured desktop frame, which apps the AI has already acted on
// this session, and whether the user's emergency stop has been triggered.
// Written by the AI-facing capture/input tools and read by the
// computer-use WebUI page, so the page can show the user what the AI just
// saw and let them halt it without the two needing a direct reference to
// each other.
class ComputerUseSessionState : public KeyedService {
 public:
  ComputerUseSessionState();
  ~ComputerUseSessionState() override;
  ComputerUseSessionState(const ComputerUseSessionState&) = delete;
  ComputerUseSessionState& operator=(const ComputerUseSessionState&) = delete;

  void SetLatestFrame(std::string frame_data_url);

  bool IsActive() const;
  const std::string& GetLatestFrameDataUrl() const;

  // One-time consent that OS-level input control (not just viewing a
  // screenshot) is allowed this conversation - separate from, and a step
  // beyond, get_desktop_screenshot's own consent. Shared across all
  // desktop_* input tools so the user isn't asked once per tool.
  void GrantInputConsent();
  bool HasInputConsent() const;

  // Apps the AI has already acted on this session, keyed by lowercase
  // process image name (e.g. "notepad.exe") - used by the risk classifier
  // to flag the *first* action against a not-yet-seen app as risky (see
  // brave-ai-computer-use.md, Phase 2, "Risk-classified confirmation").
  void MarkAppInteracted(const std::string& process_name);
  bool HasInteractedWithApp(const std::string& process_name) const;

  // The global emergency-stop hotkey (Phase 2 safety architecture item 2)
  // and the WebUI's Stop button both call this. Once set, every desktop_*
  // input tool refuses to inject further input until ResumeAfterStop() is
  // called (from the WebUI's Resume button) - a deliberately sticky halt,
  // not something the AI can just retry past.
  void EmergencyStop();
  void ResumeAfterStop();
  bool IsEmergencyStopped() const;

 private:
  bool active_ = false;
  std::string latest_frame_data_url_;
  bool input_consent_granted_ = false;
  bool emergency_stopped_ = false;
  base::flat_set<std::string> interacted_apps_;

#if BUILDFLAG(IS_WIN)
  std::unique_ptr<GlobalStopHotkey> global_stop_hotkey_;
#endif
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

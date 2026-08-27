// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_
#define BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

namespace ai_chat {
class DesktopCaptureSession;
}  // namespace ai_chat

namespace computer_use {

#if BUILDFLAG(IS_WIN)
class GlobalStopHotkey;
class RdpSession;
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
  // One completed or still-open RDP session, for the user-facing history
  // log on the computer-use page - which host:port was connected to, and
  // when it was connected/disconnected. `disconnected_at` is unset while
  // the session is still open.
  struct RdpHistoryEntry {
    RdpHistoryEntry();
    RdpHistoryEntry(std::string host,
                    int port,
                    base::Time connected_at,
                    std::optional<base::Time> disconnected_at);
    RdpHistoryEntry(const RdpHistoryEntry&);
    RdpHistoryEntry& operator=(const RdpHistoryEntry&);
    ~RdpHistoryEntry();

    std::string host;
    int port = 0;
    base::Time connected_at;
    std::optional<base::Time> disconnected_at;
  };

  ComputerUseSessionState(PrefService* prefs, base::FilePath profile_path);
  ~ComputerUseSessionState() override;
  ComputerUseSessionState(const ComputerUseSessionState&) = delete;
  ComputerUseSessionState& operator=(const ComputerUseSessionState&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void SetLatestFrame(std::string frame_data_url);

  bool IsActive() const;
  const std::string& GetLatestFrameDataUrl() const;

  // One-time consent that OS-level input control (not just viewing a
  // screenshot) is allowed this conversation - separate from, and a step
  // beyond, get_desktop_screenshot's own consent. Shared across all
  // desktop_* input tools so the user isn't asked once per tool.
  void GrantInputConsent();
  bool HasInputConsent() const;

  // Persisted (survives restart, applies to every new conversation) opt-in
  // to skip get_desktop_screenshot's per-conversation permission challenge
  // entirely - set from the computer-use WebUI's Settings toggle, off by
  // default. Deliberately a separate, explicit setting rather than
  // something a single in-conversation "Allow" click escalates to on its
  // own, since it removes a real per-conversation consent step going
  // forward.
  void SetAlwaysAllowDesktopScreenshot(bool always_allow);
  bool GetAlwaysAllowDesktopScreenshot() const;

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

#if BUILDFLAG(IS_WIN)
  // Opens (or replaces, if one's already active) the visible RDP session
  // window and connects to `host`:`port`. `callback` fires once with the
  // outcome of this connection attempt. See rdp_session.h and
  // brave-ai-computer-use.md's Phase 3 notes - this always requires a
  // fresh explicit permission challenge in the calling tool, never
  // remembered, unlike the desktop_* tools' one-time consent.
  void ConnectRdp(const std::string& host,
                  int port,
                  base::OnceCallback<void(bool, std::string)> callback);
  void DisconnectRdp();
  bool IsRdpActive() const;
  const std::string& GetRdpTargetHost() const;
  int GetRdpTargetPort() const;

  // Persisted (survives restart) log of RDP sessions on this profile, most
  // recent first - see RdpHistoryEntry.
  std::vector<RdpHistoryEntry> GetRdpHistory() const;

  // Registers the callback the WebUI push interface (ComputerUseUI) invokes
  // with a fresh data: URL each time a new frame is captured from the (now
  // hidden - see rdp_session.h) RDP session window, while one is active.
  // Only one callback is supported at a time (the WebUI mojom Page remote,
  // once bound) - a later call replaces the previous one.
  void SetRdpFrameCapturedCallback(
      base::RepeatingCallback<void(std::string)> callback);

  // Registers the callback the WebUI push interface invokes whenever RDP
  // connects or disconnects, for any reason - see mojom Page::
  // OnRdpStateChanged. Only one callback is supported at a time, like
  // SetRdpFrameCapturedCallback above.
  void SetRdpStateChangedCallback(
      base::RepeatingCallback<void(bool, std::string, int)> callback);

  // Forwards mouse/keyboard input directly to the RDP session window - see
  // RdpSession::SendMouseEvent()/SendKeyEvent()/SendCharEvent() for the
  // exact semantics. No-ops if no RDP session is active.
  void SendRdpMouseEvent(int x, int y, int buttons, int wheel_delta);
  void SendRdpKeyEvent(int virtual_key_code, bool key_down);
  void SendRdpCharEvent(char16_t character);
#endif

 private:
#if BUILDFLAG(IS_WIN)
  void OnRdpConnectResult(
      base::OnceCallback<void(bool, std::string)> callback,
      const std::string& host,
      int port,
      bool success,
      std::string error_message);
  void OnRdpDisconnected(std::string reason);

  void AppendRdpHistoryEntry(const std::string& host, int port);
  void CloseLatestOpenRdpHistoryEntry();

  // Starts/stops the ~5fps capture timer that feeds
  // `rdp_frame_captured_callback_` while an RDP session is active - started
  // on a successful connect, stopped on disconnect.
  void StartRdpCaptureTimer();
  void StopRdpCaptureTimer();
  void CaptureRdpFrameTick();
  void OnRdpFrameCaptured(bool success, std::vector<uint8_t> png_bytes);
#endif

  bool active_ = false;
  std::string latest_frame_data_url_;
  bool input_consent_granted_ = false;
  bool emergency_stopped_ = false;
  base::flat_set<std::string> interacted_apps_;
  raw_ptr<PrefService> prefs_;
  base::FilePath profile_path_;

#if BUILDFLAG(IS_WIN)
  std::unique_ptr<GlobalStopHotkey> global_stop_hotkey_;
  std::unique_ptr<RdpSession> rdp_session_;
  bool rdp_active_ = false;
  std::string rdp_target_host_;
  int rdp_target_port_ = 0;

  std::unique_ptr<ai_chat::DesktopCaptureSession> rdp_capture_session_;
  base::RepeatingTimer rdp_capture_timer_;
  base::RepeatingCallback<void(std::string)> rdp_frame_captured_callback_;
  base::RepeatingCallback<void(bool, std::string, int)>
      rdp_state_changed_callback_;
  base::WeakPtrFactory<ComputerUseSessionState> rdp_capture_weak_ptr_factory_{
      this};
#endif
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_COMPUTER_USE_SESSION_STATE_H_

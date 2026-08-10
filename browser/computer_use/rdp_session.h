// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_
#define BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"

namespace computer_use {

// Hosts Microsoft's RDP ActiveX control (MsTscAx) in a real, visible,
// top-level window titled "RDP: <host> - AI Automation Browser" - not
// hidden, and not embedded inside any Chromium tab/view (true tab
// embedding needs native-child-window mixing into the Views/Aura
// hierarchy, a much larger undertaking deferred past this first cut - see
// brave-ai-computer-use.md's Phase 3 notes). Being a real on-screen window
// is enough for it to compose for free with the rest of this feature:
// get_desktop_screenshot already captures whatever's on screen, and the
// desktop_* input tools already target whatever's on screen via SendInput
// - neither needs to know or care that a window happens to be an RDP
// session rather than a local app.
//
// Adapted from remoting/host/win/rdp_client_window.cc's ActiveX-hosting
// pattern (not linked - remoting/'s DEPS forbids external dependents;
// com_imported_mstscax.h, the MIDL-generated COM interface header that
// pattern depends on, is copied into browser/computer_use/win/ for the
// same reason). Deliberately does NOT port that file's window-hiding or
// its WH_CBT hook that auto-dismisses any dialog the RDP control shows -
// this is a real interactive session a human should see, including its
// certificate/trust warnings, not a headless one. Also deliberately never
// touches a password: host/port are the only inputs this class takes:
// authentication is handled by the RDP control's own native prompt (or by
// Windows Credential Manager if the user has already saved credentials for
// that host, exactly as mstsc.exe itself would use them) - the browser
// process's memory never holds an RDP password.
//
// Must be constructed, used, and destroyed from a UI-message-pumped
// thread (COM STA + a real window) - the browser's UI thread qualifies.
class RdpSession {
 public:
  // `success` is false and `error_message` non-empty if the connection
  // attempt itself failed (bad host, refused, auth failed then the user
  // closed the credential prompt, etc.). Fires exactly once, for the
  // outcome of this specific Connect() call - not for later disconnects
  // once already connected (see SetDisconnectedCallback for that).
  using ConnectedCallback =
      base::OnceCallback<void(bool success, std::string error_message)>;

  // Fires whenever an established session ends, for any reason (user
  // closed the window, server disconnected, network error) - lets the
  // owner clear its "RDP active" state regardless of why.
  using DisconnectedCallback =
      base::RepeatingCallback<void(std::string reason)>;

  RdpSession();
  ~RdpSession();
  RdpSession(const RdpSession&) = delete;
  RdpSession& operator=(const RdpSession&) = delete;

  void Connect(const std::string& host, int port, ConnectedCallback callback);

  // Requests a graceful disconnect; the window closes once done. Safe to
  // call even if not connected yet.
  void Disconnect();

  bool IsConnected() const;

  void SetDisconnectedCallback(DisconnectedCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_RDP_SESSION_H_

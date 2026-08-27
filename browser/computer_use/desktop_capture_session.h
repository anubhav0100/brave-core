// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_DESKTOP_CAPTURE_SESSION_H_
#define BRAVE_BROWSER_COMPUTER_USE_DESKTOP_CAPTURE_SESSION_H_

#include <cstdint>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"

namespace ai_chat {

// Captures a single still screenshot of the full host desktop (all monitors
// combined) on demand - see tools/computer_use/get_desktop_screenshot_tool.h
// for the AI-facing tool built on this, and brave-ai-computer-use.md (design
// doc) for why a still screenshot rather than a live video stream: simpler,
// sufficient for an observe-decide-act tool loop, and far lower risk to get
// right for this feature's first phase (see the doc for the full phased plan
// and the safety architecture later phases build on top of this).
class DesktopCaptureSession {
 public:
  // `success` is false if capture failed for any reason (no permission, no
  // capturer available on this platform, capture error, encoding failure);
  // `png_bytes` is only meaningful when `success` is true.
  using ScreenshotCallback =
      base::OnceCallback<void(bool success, std::vector<uint8_t> png_bytes)>;

  DesktopCaptureSession();
  ~DesktopCaptureSession();

  DesktopCaptureSession(const DesktopCaptureSession&) = delete;
  DesktopCaptureSession& operator=(const DesktopCaptureSession&) = delete;

  // Captures one screenshot and returns it PNG-encoded via `callback`, which
  // runs on the calling sequence. A fresh webrtc::DesktopCapturer is created
  // and torn down for each call rather than kept alive across calls - the
  // capturer must always be driven from a single consistent thread (mirrors
  // content::DesktopCaptureDevice's own threading model, since the
  // underlying GDI/DXGI capture APIs assume it), and recreating it per call
  // avoids that lifetime complexity entirely for a low, one-off cost that's
  // negligible against "let the AI look at the screen" being an occasional
  // action, not continuous video.
  void CaptureScreenshot(ScreenshotCallback callback);

  // Like CaptureScreenshot(), but captures one specific window (`window_id`,
  // a platform window handle - an HWND on Windows) instead of the full
  // desktop. Uses content::desktop_capture::CreateWindowCapturer(), which
  // (unlike the full-desktop path) already handles hardware-accelerated/
  // DirectX-rendered window content correctly on its own - no
  // PrintWindow-overlay compositing needed here. Used for the RDP session
  // window (rdp_session.h), which is never shown on screen, so a
  // full-desktop capture wouldn't show it at all.
  void CaptureWindow(intptr_t window_id, ScreenshotCallback callback);

 private:
  scoped_refptr<base::SingleThreadTaskRunner> capture_task_runner_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_COMPUTER_USE_DESKTOP_CAPTURE_SESSION_H_

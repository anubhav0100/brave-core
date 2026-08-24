// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/desktop_capture_session.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/desktop_capture.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_types.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "ui/gfx/codec/png_codec.h"

namespace ai_chat {

namespace {

// `frame`'s pixel format is BGRA (webrtc::FOURCC_ARGB in little-endian byte
// order), matching SkBitmap's native 32-bit format and
// EncodeBGRASkBitmap's expectation - same conversion
// chrome/browser/media/webrtc/native_desktop_media_list.cc's
// ScaleDesktopFrame() uses for its (scaled) thumbnails, just without the
// scaling since this wants the full-resolution frame.
std::vector<uint8_t> EncodeFrameAsPng(webrtc::DesktopFrame* frame) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(frame->size().width(), frame->size().height(),
                        /*isOpaque=*/true);
  libyuv::ARGBCopy(frame->data(), frame->stride(),
                   reinterpret_cast<uint8_t*>(bitmap.getPixels()),
                   static_cast<int>(bitmap.rowBytes()), frame->size().width(),
                   frame->size().height());
  return gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                           /*discard_transparency=*/true)
      .value_or(std::vector<uint8_t>());
}

// Owns a webrtc::DesktopCapturer for the lifetime of exactly one screenshot
// capture. Created, used, and destroyed entirely on the capture task
// runner (see DesktopCaptureSession's header comment for why). Manages its
// own lifetime via `self_` - a well-established self-deleting pattern
// already used elsewhere in this fork's callback-driven code (e.g. the n8n
// backup save/open file dialog listeners) - so DesktopCaptureSession itself
// never needs to track or outlive this object.
class OneShotCapturer : public webrtc::DesktopCapturer::Callback {
 public:
  // ERROR_TEMPORARY is webrtc's own documented signal to just call
  // CaptureFrame() again (see desktop_capturer.h's Result enum) - real on
  // Windows, where the first DXGI Desktop Duplication frame after starting
  // duplication (or right after a mode/resolution change) commonly comes
  // back this way. Treating it as a hard failure on the very first attempt,
  // as this used to, meant a perfectly capturable desktop could still
  // report "capture failed" to the user for a condition webrtc itself says
  // to just retry past.
  static constexpr int kMaxTemporaryErrorRetries = 3;

  static void CaptureOnCaptureSequence(
      DesktopCaptureSession::ScreenshotCallback callback) {
    auto owned = std::make_unique<OneShotCapturer>();
    OneShotCapturer* raw = owned.get();
    raw->callback_ = std::move(callback);
    raw->self_ = std::move(owned);

    raw->capturer_ = content::desktop_capture::CreateScreenCapturer(
        content::desktop_capture::CreateDesktopCaptureOptions(),
        /*for_snapshot=*/true);
    if (!raw->capturer_) {
      LOG(ERROR) << "computer_use: no desktop capturer is available on this "
                    "platform/session (CreateScreenCapturer returned null)";
      raw->Finish(false, {});
      return;
    }
    raw->capturer_->SelectSource(webrtc::kFullDesktopScreenId);
    raw->capturer_->Start(raw);
    raw->capturer_->CaptureFrame();
  }

  OneShotCapturer() = default;

 private:
  void OnCaptureResult(
      webrtc::DesktopCapturer::Result result,
      std::unique_ptr<webrtc::DesktopFrame> frame) override {
    if (result == webrtc::DesktopCapturer::Result::ERROR_TEMPORARY &&
        temporary_error_retries_ < kMaxTemporaryErrorRetries) {
      ++temporary_error_retries_;
      LOG(WARNING) << "computer_use: desktop capture returned a temporary "
                      "error (attempt "
                   << temporary_error_retries_ << "/"
                   << kMaxTemporaryErrorRetries << "), retrying";
      capturer_->CaptureFrame();
      return;
    }
    if (result != webrtc::DesktopCapturer::Result::SUCCESS || !frame) {
      LOG(ERROR) << "computer_use: desktop capture failed (result="
                 << static_cast<int>(result) << ", has_frame=" << !!frame
                 << ") - common causes on Windows: the session has no real "
                    "GPU-backed display surface (some remote/virtual "
                    "sessions), or the screen was locked/showing a UAC "
                    "secure desktop at the moment of capture";
      Finish(false, {});
      return;
    }
    std::vector<uint8_t> png_bytes = EncodeFrameAsPng(frame.get());
    if (png_bytes.empty()) {
      LOG(ERROR) << "computer_use: captured a frame but PNG-encoding it "
                    "failed";
    }
    Finish(!png_bytes.empty(), std::move(png_bytes));
  }

  void Finish(bool success, std::vector<uint8_t> png_bytes) {
    std::move(callback_).Run(success, std::move(png_bytes));
    self_.reset();  // Deletes `this`.
  }

  std::unique_ptr<webrtc::DesktopCapturer> capturer_;
  DesktopCaptureSession::ScreenshotCallback callback_;
  std::unique_ptr<OneShotCapturer> self_;
  int temporary_error_retries_ = 0;
};

}  // namespace

DesktopCaptureSession::DesktopCaptureSession()
    : capture_task_runner_(base::ThreadPool::CreateSingleThreadTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::SingleThreadTaskRunnerThreadMode::DEDICATED)) {}

DesktopCaptureSession::~DesktopCaptureSession() = default;

void DesktopCaptureSession::CaptureScreenshot(ScreenshotCallback callback) {
  capture_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &OneShotCapturer::CaptureOnCaptureSequence,
          base::BindPostTaskToCurrentDefault(std::move(callback))));
}

}  // namespace ai_chat

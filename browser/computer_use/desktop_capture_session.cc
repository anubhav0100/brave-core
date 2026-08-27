// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/desktop_capture_session.h"

#include <iterator>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "content/public/browser/desktop_capture.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_types.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "ui/gfx/codec/png_codec.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/win/scoped_hdc.h"
#include "brave/browser/computer_use/rdp_session.h"

// Only defined when the target Windows SDK version macros are high enough;
// this build's exact value (0x00000002) is fixed by Microsoft's own
// documentation for PrintWindow, so defining it directly here if missing is
// safe regardless of WINVER/NTDDI_VERSION.
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

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

#if BUILDFLAG(IS_WIN)
// GDI's BitBlt-based capture (webrtc's automatic fallback when DXGI Desktop
// Duplication isn't available on this hardware/driver - see
// ScreenCapturerWinGdi) can't correctly capture hardware-accelerated/
// DirectX-rendered window surfaces, which come back as solid black - this
// notably includes the RDP ActiveX control's own window (rdp_session.cc).
// PrintWindow's PW_RENDERFULLCONTENT flag (Windows 8.1+) is specifically
// designed to render that kind of content correctly, so overlay a window's
// PrintWindow output onto the already-captured frame - cheap (one window,
// not the whole desktop composited again).
//
// Known tradeoff: if some other topmost/always-on-top window partially
// covers this one, this paints over that overlapping region with this
// window's content regardless (PrintWindow renders a window's content
// irrespective of on-screen occlusion) - a narrow edge case, and still
// strictly better than the black rectangle it replaces. Safe to call
// unconditionally even when the base capture was already correct (DXGI
// succeeded) - at worst it's redundant work with that same edge-case
// tradeoff, never a crash.
void CompositeWindowIfPossible(webrtc::DesktopFrame* frame, HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    return;
  }
  RECT window_rect;
  if (!GetWindowRect(hwnd, &window_rect)) {
    return;
  }
  int width = window_rect.right - window_rect.left;
  int height = window_rect.bottom - window_rect.top;
  if (width <= 0 || height <= 0) {
    return;
  }

  base::win::ScopedGetDC screen_dc(nullptr);
  base::win::ScopedCreateDC mem_dc(CreateCompatibleDC(screen_dc));
  if (!mem_dc.is_valid()) {
    return;
  }
  HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
  if (!bitmap) {
    return;
  }
  HGDIOBJ old_bitmap = SelectObject(mem_dc.Get(), bitmap);

  bool printed = !!PrintWindow(hwnd, mem_dc.Get(), PW_RENDERFULLCONTENT);

  std::vector<uint8_t> window_pixels;
  int lines_copied = 0;
  if (printed) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Negative for top-down rows.
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    window_pixels.resize(static_cast<size_t>(width) * height * 4);
    lines_copied = GetDIBits(mem_dc.Get(), bitmap, 0, height,
                             window_pixels.data(), &bmi, DIB_RGB_COLORS);
  }

  SelectObject(mem_dc.Get(), old_bitmap);
  DeleteObject(bitmap);

  if (!printed || lines_copied != height) {
    return;
  }

  webrtc::DesktopRect window_dest_rect = webrtc::DesktopRect::MakeLTRB(
      window_rect.left, window_rect.top, window_rect.right,
      window_rect.bottom);
  window_dest_rect.IntersectWith(webrtc::DesktopRect::MakeSize(frame->size()));
  if (window_dest_rect.is_empty()) {
    return;
  }

  base::span<const uint8_t> window_pixels_span(window_pixels);
  size_t copy_len = static_cast<size_t>(window_dest_rect.width()) * 4;
  for (int y = window_dest_rect.top(); y < window_dest_rect.bottom(); ++y) {
    int src_y = y - window_rect.top;
    size_t src_offset =
        static_cast<size_t>(src_y) * width * 4 +
        static_cast<size_t>(window_dest_rect.left() - window_rect.left) * 4;
    auto src_row = window_pixels_span.subspan(src_offset, copy_len);
    uint8_t* dest_row = frame->GetFrameDataAtPos(
        webrtc::DesktopVector(window_dest_rect.left(), y));
    // SAFETY: `dest_row` points into `frame`'s pixel buffer at row `y`,
    // which is within `frame->size()` since `window_dest_rect` was
    // intersected with it above; `copy_len` is that same rect's row width
    // in bytes, so it can't read past the row (or the frame, since y is
    // also bounded).
    auto dest_row_span =
        UNSAFE_BUFFERS(base::span<uint8_t>(dest_row, copy_len));
    dest_row_span.copy_from(src_row);
  }
}

BOOL CALLBACK CompositeIfRdpSessionWindow(HWND hwnd, LPARAM lparam) {
  wchar_t class_name[256];
  int len = GetClassNameW(hwnd, class_name, std::size(class_name));
  if (len > 0 &&
      std::wstring_view(class_name, static_cast<size_t>(len)) ==
          computer_use::kRdpSessionWindowClassName) {
    CompositeWindowIfPossible(reinterpret_cast<webrtc::DesktopFrame*>(lparam),
                              hwnd);
  }
  return TRUE;  // Keep enumerating - there's normally only one, but this
                // doesn't assume that.
}

// The screenshot tool is almost always invoked from the AI Chat conversation
// itself, which is part of the browser's own window - meaning
// GetForegroundWindow() (input focus) is normally the browser, not whatever
// separate window (like an RDP session, rdp_session.cc) the user/AI actually
// wants captured. Compositing the foreground window still helps in the less
// common case where something else genuinely has focus, but finding the RDP
// session window specifically by its known Win32 class name - regardless of
// focus - is what actually matters for the common case this feature exists
// for.
void CompositeHardwareRenderedWindows(webrtc::DesktopFrame* frame) {
  CompositeWindowIfPossible(frame, GetForegroundWindow());
  EnumWindows(&CompositeIfRdpSessionWindow, reinterpret_cast<LPARAM>(frame));
}
#endif  // BUILDFLAG(IS_WIN)

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
    raw->composite_hardware_rendered_windows_ = true;
    raw->capturer_->SelectSource(webrtc::kFullDesktopScreenId);
    raw->capturer_->Start(raw);
    raw->capturer_->CaptureFrame();
  }

  // Like CaptureOnCaptureSequence(), but captures one specific window
  // (`window_id`) instead of the full desktop - see
  // DesktopCaptureSession::CaptureWindow()'s header comment.
  static void CaptureWindowOnCaptureSequence(
      intptr_t window_id,
      DesktopCaptureSession::ScreenshotCallback callback) {
    auto owned = std::make_unique<OneShotCapturer>();
    OneShotCapturer* raw = owned.get();
    raw->callback_ = std::move(callback);
    raw->self_ = std::move(owned);

    raw->capturer_ = content::desktop_capture::CreateWindowCapturer(
        content::desktop_capture::CreateDesktopCaptureOptions());
    if (!raw->capturer_) {
      LOG(ERROR) << "computer_use: no window capturer is available on this "
                    "platform (CreateWindowCapturer returned null)";
      raw->Finish(false, {});
      return;
    }
    if (!raw->capturer_->SelectSource(
            static_cast<webrtc::DesktopCapturer::SourceId>(window_id))) {
      LOG(ERROR) << "computer_use: window capturer rejected the requested "
                    "window (it may have been closed)";
      raw->Finish(false, {});
      return;
    }
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
#if BUILDFLAG(IS_WIN)
    if (composite_hardware_rendered_windows_) {
      CompositeHardwareRenderedWindows(frame.get());
    }
#endif
    std::vector<uint8_t> png_bytes = EncodeFrameAsPng(frame.get());
    // `encode_success` must be computed into its own variable, in its own
    // statement, before the Finish() call below - evaluation order between
    // a function call's arguments is unspecified, so
    // `Finish(!png_bytes.empty(), std::move(png_bytes))` risked the
    // compiler binding the moved-from `png_bytes` argument before
    // evaluating `.empty()` on the (now emptied) source, silently turning
    // every successful capture into a reported failure despite the
    // callee still receiving the real, complete image data. Confirmed via
    // live testing - not a hypothetical.
    bool encode_success = !png_bytes.empty();
    if (!encode_success) {
      LOG(ERROR) << "computer_use: captured a frame but PNG-encoding it "
                    "failed";
    }
    Finish(encode_success, std::move(png_bytes));
  }

  void Finish(bool success, std::vector<uint8_t> png_bytes) {
    std::move(callback_).Run(success, std::move(png_bytes));
    self_.reset();  // Deletes `this`.
  }

  std::unique_ptr<webrtc::DesktopCapturer> capturer_;
  DesktopCaptureSession::ScreenshotCallback callback_;
  std::unique_ptr<OneShotCapturer> self_;
  int temporary_error_retries_ = 0;
  // Only set for the full-desktop capture path - the window-specific path
  // (content::desktop_capture::CreateWindowCapturer()) already handles
  // hardware-accelerated content correctly on its own.
  bool composite_hardware_rendered_windows_ = false;
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

void DesktopCaptureSession::CaptureWindow(intptr_t window_id,
                                          ScreenshotCallback callback) {
  capture_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &OneShotCapturer::CaptureWindowOnCaptureSequence, window_id,
          base::BindPostTaskToCurrentDefault(std::move(callback))));
}

}  // namespace ai_chat

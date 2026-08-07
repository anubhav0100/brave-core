// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_PAGE_CAPTURE_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_PAGE_CAPTURE_TOOLS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace ai_chat {

class PageCaptureSession;

// Captures the current active tab's actual page source (not text the model
// writes/retypes itself) into a shared, conversation-scoped session buffer -
// split into sections by heading, plus its links and images - without
// showing any dialog. Call save_captured_session_as_word_document to write
// everything captured so far to disk; call this again first to add more
// pages before saving.
class CapturePageToSessionTool : public Tool {
 public:
  explicit CapturePageToSessionTool(PageCaptureSession* session);
  ~CapturePageToSessionTool() override;

  CapturePageToSessionTool(const CapturePageToSessionTool&) = delete;
  CapturePageToSessionTool& operator=(const CapturePageToSessionTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnCaptureComplete(UseToolCallback callback,
                        bool success,
                        std::string message);

  raw_ptr<PageCaptureSession> session_ = nullptr;

  base::WeakPtrFactory<CapturePageToSessionTool> weak_ptr_factory_{this};
};

// Builds one Word (.docx) document from every page captured into the
// session so far (each as its own titled section, with real embedded
// pictures for any images found), and saves it via a native Save As dialog.
// Picking the same filename as an existing document lets the user overwrite
// it to update it. Does not clear the session - more pages can be captured
// and this called again (e.g. to save to a different file, or update the
// same one).
class SaveCapturedSessionAsWordDocumentTool : public Tool {
 public:
  explicit SaveCapturedSessionAsWordDocumentTool(PageCaptureSession* session);
  ~SaveCapturedSessionAsWordDocumentTool() override;

  SaveCapturedSessionAsWordDocumentTool(
      const SaveCapturedSessionAsWordDocumentTool&) = delete;
  SaveCapturedSessionAsWordDocumentTool& operator=(
      const SaveCapturedSessionAsWordDocumentTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnSaveComplete(UseToolCallback callback,
                      bool success,
                      std::string message);

  raw_ptr<PageCaptureSession> session_ = nullptr;

  base::WeakPtrFactory<SaveCapturedSessionAsWordDocumentTool>
      weak_ptr_factory_{this};
};

// Clears the page capture session, discarding everything captured so far so
// the next capture starts a fresh document.
class ClearCapturedSessionTool : public Tool {
 public:
  explicit ClearCapturedSessionTool(PageCaptureSession* session);
  ~ClearCapturedSessionTool() override;

  ClearCapturedSessionTool(const ClearCapturedSessionTool&) = delete;
  ClearCapturedSessionTool& operator=(const ClearCapturedSessionTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<PageCaptureSession> session_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_PAGE_CAPTURE_TOOLS_H_

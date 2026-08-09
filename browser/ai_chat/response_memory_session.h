// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_RESPONSE_MEMORY_SESSION_H_
#define BRAVE_BROWSER_AI_CHAT_RESPONSE_MEMORY_SESSION_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// One response the assistant chose to save into a ResponseMemorySession -
// `label` becomes that entry's heading in the saved document.
struct MemoryEntry {
  std::string label;
  std::string text;
};

// Accumulates text the assistant itself has generated (an answer, a draft,
// a summary - not fetched page content, see PageCaptureSession for that)
// across one conversation, so several responses can be merged into one
// Word document on request instead of the user having to copy each one out
// by hand. Owned by BrowserToolProvider so it's shared by
// SaveResponseToMemoryTool, SaveResponseMemoryAsWordDocumentTool, and
// ClearResponseMemoryTool, and lives for the conversation.
class ResponseMemorySession {
 public:
  using ResultCallback =
      base::OnceCallback<void(bool success, std::string message)>;

  explicit ResponseMemorySession(content::BrowserContext* browser_context);
  ~ResponseMemorySession();

  ResponseMemorySession(const ResponseMemorySession&) = delete;
  ResponseMemorySession& operator=(const ResponseMemorySession&) = delete;

  // Appends one entry. Does not save anything to disk yet.
  void AddResponse(const std::string& label, const std::string& text);

  // Builds one Word document from every response saved so far (each as its
  // own titled section, in the order they were added), and shows a native
  // Save As dialog. Does not clear the session - more responses can be
  // added and this called again.
  void SaveAsWordDocument(const std::string& filename, ResultCallback callback);

  void Clear();

  size_t entry_count() const { return entries_.size(); }

 private:
  void OnSaveComplete(ResultCallback callback,
                      std::string filename,
                      DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::vector<MemoryEntry> entries_;

  base::WeakPtrFactory<ResponseMemorySession> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_RESPONSE_MEMORY_SESSION_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PDF_DOCUMENT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PDF_DOCUMENT_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Exposes a tool that lets the assistant create a PDF from HTML content it
// provides and triggers a native browser download of the result. Unlike
// the Word/Excel/PowerPoint tools, this doesn't hand-build a file format -
// it reuses Chromium's real print-to-pdf pipeline (the same code behind
// `--headless --print-to-pdf` and DevTools Page.printToPDF) by loading the
// HTML into a hidden, never-visible content::WebContents and printing
// that. This is the highest-fidelity of the four document tools since it's
// real Blink layout/rendering - the assistant can pass headings, tables,
// and CSS, not just plain text.
//
// Each UseTool() call spawns an independent, self-deleting
// PdfDocumentGenerator (see create_pdf_document_tool.cc) rather than
// storing per-call state on this Tool, since a single Tool instance is
// long-lived and can in principle be invoked again (e.g. by a different
// conversation sharing the same profile) before a prior call's generation
// finishes.
class CreatePdfDocumentTool : public Tool {
 public:
  explicit CreatePdfDocumentTool(content::BrowserContext* browser_context);
  ~CreatePdfDocumentTool() override;

  CreatePdfDocumentTool(const CreatePdfDocumentTool&) = delete;
  CreatePdfDocumentTool& operator=(const CreatePdfDocumentTool&) = delete;

  // Tool:
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnPdfGenerated(UseToolCallback callback,
                      std::string filename,
                      std::vector<uint8_t> pdf_bytes,
                      std::string error_message);
  void OnDownloadComplete(UseToolCallback callback,
                          std::string filename,
                          DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<CreatePdfDocumentTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PDF_DOCUMENT_TOOL_H_

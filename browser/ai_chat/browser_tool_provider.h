// Copyright (c) 2025 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_BROWSER_TOOL_PROVIDER_H_
#define BRAVE_BROWSER_AI_CHAT_BROWSER_TOOL_PROVIDER_H_

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/browser/tools/tool_provider.h"
#include "brave/components/ai_chat/core/common/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"

class Profile;

namespace content {
class BrowserContext;
}

namespace ai_chat {

class CapturePageToSessionTool;
class ClearCapturedSessionTool;
class CodeExecutionTool;
#if BUILDFLAG(ENABLE_PRINTING)
class CreatePdfDocumentTool;
#endif
class CreatePresentationTool;
class CreateSpreadsheetTool;
class CreateWordDocumentTool;
class ReadWordDocumentTool;
class HistorySearchTool;
class ClearResponseMemoryTool;
class PageCaptureSession;
class ResponseMemorySession;
class RunWorkflowTool;
class SaveCapturedSessionAsWordDocumentTool;
class SaveResponseMemoryAsWordDocumentTool;
class SaveResponseToMemoryTool;
class SearchIndexedContentTool;
class TabManagementTool;

// Implementation of ToolProvider that provides browser-specific
// tools for conversations.
// It is responsible for grouping browser action tasks (a set of tabs)
// that the tools for a conversation perform actions on.
class BrowserToolProvider : public ToolProvider {
 public:
  explicit BrowserToolProvider(Profile* profile);

  ~BrowserToolProvider() override;

  BrowserToolProvider(const BrowserToolProvider&) = delete;
  BrowserToolProvider& operator=(const BrowserToolProvider&) = delete;

  // ToolProvider implementation
  std::vector<base::WeakPtr<Tool>> GetTools() override;

  HistorySearchTool* GetHistorySearchToolForTesting();

 private:
  void CreateTools(content::BrowserContext* browser_context);

  // Browser-specific tools owned by this provider
  std::unique_ptr<CodeExecutionTool> code_execution_tool_;
  std::unique_ptr<CreateWordDocumentTool> create_word_document_tool_;
  std::unique_ptr<ReadWordDocumentTool> read_word_document_tool_;
  std::unique_ptr<PageCaptureSession> page_capture_session_;
  std::unique_ptr<CapturePageToSessionTool> capture_page_to_session_tool_;
  std::unique_ptr<SaveCapturedSessionAsWordDocumentTool>
      save_captured_session_as_word_document_tool_;
  std::unique_ptr<ClearCapturedSessionTool> clear_captured_session_tool_;
  std::unique_ptr<ResponseMemorySession> response_memory_session_;
  std::unique_ptr<SaveResponseToMemoryTool> save_response_to_memory_tool_;
  std::unique_ptr<SaveResponseMemoryAsWordDocumentTool>
      save_response_memory_as_word_document_tool_;
  std::unique_ptr<ClearResponseMemoryTool> clear_response_memory_tool_;
  std::unique_ptr<SearchIndexedContentTool> search_indexed_content_tool_;
  std::unique_ptr<RunWorkflowTool> run_workflow_tool_;
  std::unique_ptr<CreateSpreadsheetTool> create_spreadsheet_tool_;
  std::unique_ptr<CreatePresentationTool> create_presentation_tool_;
#if BUILDFLAG(ENABLE_PRINTING)
  std::unique_ptr<CreatePdfDocumentTool> create_pdf_document_tool_;
#endif
  std::unique_ptr<HistorySearchTool> history_search_tool_;
#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
  std::unique_ptr<TabManagementTool> tab_management_tool_;
#endif
  raw_ptr<Profile> profile_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_BROWSER_TOOL_PROVIDER_H_

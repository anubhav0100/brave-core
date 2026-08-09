// Copyright (c) 2025 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/browser_tool_provider.h"

#include <memory>
#include <vector>

#include "base/check_is_test.h"
#include "base/feature_list.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"
#include "brave/browser/ai_chat/page_capture_session.h"
#include "brave/browser/ai_chat/response_memory_session.h"
#include "brave/browser/ai_chat/tools/code_execution_tool.h"
#include "brave/browser/ai_chat/tools/content_index_tools.h"
#include "brave/browser/ai_chat/tools/create_presentation_tool.h"
#include "brave/browser/ai_chat/tools/create_spreadsheet_tool.h"
#include "brave/browser/ai_chat/tools/create_word_document_tool.h"
#include "brave/browser/ai_chat/tools/history_search_tool.h"
#include "brave/browser/ai_chat/tools/page_capture_tools.h"
#include "brave/browser/ai_chat/tools/read_word_document_tool.h"
#include "brave/browser/ai_chat/tools/response_memory_tools.h"
#include "brave/browser/ai_chat/tools/run_workflow_tool.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/buildflags/buildflags.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "chrome/browser/history_embeddings/history_embeddings_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "printing/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
#include "brave/browser/ai_chat/tools/tab_management_tool.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "brave/browser/ai_chat/tools/create_pdf_document_tool.h"
#endif

namespace ai_chat {

BrowserToolProvider::BrowserToolProvider(Profile* profile) : profile_(profile) {
  CreateTools(profile);
}

BrowserToolProvider::~BrowserToolProvider() = default;

std::vector<base::WeakPtr<Tool>> BrowserToolProvider::GetTools() {
  std::vector<base::WeakPtr<Tool>> tool_ptrs;
  if (code_execution_tool_) {
    tool_ptrs.push_back(code_execution_tool_->GetWeakPtr());
  }
  if (history_search_tool_) {
    tool_ptrs.push_back(history_search_tool_->GetWeakPtr());
  }
  if (create_word_document_tool_) {
    tool_ptrs.push_back(create_word_document_tool_->GetWeakPtr());
  }
  if (read_word_document_tool_) {
    tool_ptrs.push_back(read_word_document_tool_->GetWeakPtr());
  }
  if (capture_page_to_session_tool_) {
    tool_ptrs.push_back(capture_page_to_session_tool_->GetWeakPtr());
  }
  if (save_captured_session_as_word_document_tool_) {
    tool_ptrs.push_back(
        save_captured_session_as_word_document_tool_->GetWeakPtr());
  }
  if (clear_captured_session_tool_) {
    tool_ptrs.push_back(clear_captured_session_tool_->GetWeakPtr());
  }
  if (save_response_to_memory_tool_) {
    tool_ptrs.push_back(save_response_to_memory_tool_->GetWeakPtr());
  }
  if (save_response_memory_as_word_document_tool_) {
    tool_ptrs.push_back(
        save_response_memory_as_word_document_tool_->GetWeakPtr());
  }
  if (clear_response_memory_tool_) {
    tool_ptrs.push_back(clear_response_memory_tool_->GetWeakPtr());
  }
  if (search_indexed_content_tool_) {
    tool_ptrs.push_back(search_indexed_content_tool_->GetWeakPtr());
  }
  if (run_workflow_tool_) {
    tool_ptrs.push_back(run_workflow_tool_->GetWeakPtr());
  }
  if (create_spreadsheet_tool_) {
    tool_ptrs.push_back(create_spreadsheet_tool_->GetWeakPtr());
  }
  if (create_presentation_tool_) {
    tool_ptrs.push_back(create_presentation_tool_->GetWeakPtr());
  }
#if BUILDFLAG(ENABLE_PRINTING)
  if (create_pdf_document_tool_) {
    tool_ptrs.push_back(create_pdf_document_tool_->GetWeakPtr());
  }
#endif

#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
  if (tab_management_tool_) {
    tool_ptrs.push_back(tab_management_tool_->GetWeakPtr());
  }
#endif

  return tool_ptrs;
}

HistorySearchTool* BrowserToolProvider::GetHistorySearchToolForTesting() {
  CHECK_IS_TEST();
  return history_search_tool_.get();
}

void BrowserToolProvider::CreateTools(
    content::BrowserContext* browser_context) {
  if (features::IsCodeExecutionToolEnabled()) {
    code_execution_tool_ = std::make_unique<CodeExecutionTool>(browser_context);
  }
  if (history_embeddings::IsHistoryEmbeddingsEnabledForProfile(
          Profile::FromBrowserContext(browser_context))) {
    history_search_tool_ = std::make_unique<HistorySearchTool>(browser_context);
  }
  create_word_document_tool_ =
      std::make_unique<CreateWordDocumentTool>(browser_context);
  read_word_document_tool_ =
      std::make_unique<ReadWordDocumentTool>(browser_context);
  page_capture_session_ =
      std::make_unique<PageCaptureSession>(browser_context);
  capture_page_to_session_tool_ =
      std::make_unique<CapturePageToSessionTool>(page_capture_session_.get());
  save_captured_session_as_word_document_tool_ =
      std::make_unique<SaveCapturedSessionAsWordDocumentTool>(
          page_capture_session_.get());
  clear_captured_session_tool_ = std::make_unique<ClearCapturedSessionTool>(
      page_capture_session_.get());
  response_memory_session_ =
      std::make_unique<ResponseMemorySession>(browser_context);
  save_response_to_memory_tool_ = std::make_unique<SaveResponseToMemoryTool>(
      response_memory_session_.get());
  save_response_memory_as_word_document_tool_ =
      std::make_unique<SaveResponseMemoryAsWordDocumentTool>(
          response_memory_session_.get());
  clear_response_memory_tool_ = std::make_unique<ClearResponseMemoryTool>(
      response_memory_session_.get());
  search_indexed_content_tool_ =
      std::make_unique<SearchIndexedContentTool>(
          AiChatContentIndexFactory::GetForBrowserContext(browser_context));
  run_workflow_tool_ = std::make_unique<RunWorkflowTool>(browser_context);
  create_spreadsheet_tool_ =
      std::make_unique<CreateSpreadsheetTool>(browser_context);
  create_presentation_tool_ =
      std::make_unique<CreatePresentationTool>(browser_context);
#if BUILDFLAG(ENABLE_PRINTING)
  create_pdf_document_tool_ =
      std::make_unique<CreatePdfDocumentTool>(browser_context);
#endif
#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
  if (base::FeatureList::IsEnabled(features::kTabManagementTool)) {
    tab_management_tool_ = std::make_unique<TabManagementTool>(profile_);
  }
#endif
}

}  // namespace ai_chat

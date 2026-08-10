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
#include "brave/browser/ai_chat/tools/computer_use/get_desktop_screenshot_tool.h"
#if BUILDFLAG(IS_WIN)
#include "brave/browser/ai_chat/tools/computer_use/close_rdp_session_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/desktop_click_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/desktop_move_mouse_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/desktop_press_key_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/desktop_scroll_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/desktop_type_text_tool.h"
#include "brave/browser/ai_chat/tools/computer_use/open_rdp_session_tool.h"
#endif
#include "brave/browser/ai_chat/tools/delegation_tools.h"
#include "brave/browser/delegation/delegation_process_manager_factory.h"
#include "brave/browser/ai_chat/tools/history_search_tool.h"
#include "brave/browser/ai_chat/tools/n8n_mcp_tools.h"
#include "brave/browser/ai_chat/tools/n8n_sync_tools.h"
#include "brave/browser/ai_chat/tools/n8n_tools.h"
#include "brave/browser/n8n/n8n_process_manager_factory.h"
#include "brave/browser/ai_chat/tools/page_capture_tools.h"
#include "brave/browser/ai_chat/tools/read_word_document_tool.h"
#include "brave/browser/ai_chat/tools/response_memory_tools.h"
#include "brave/browser/ai_chat/tools/run_workflow_tool.h"
#include "brave/browser/ai_chat/tools/subagent_tool.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "brave/components/ai_chat/core/common/buildflags/buildflags.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "chrome/browser/history_embeddings/history_embeddings_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "build/build_config.h"
#include "printing/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
#include "brave/browser/ai_chat/tools/tab_management_tool.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "brave/browser/ai_chat/tools/create_pdf_document_tool.h"
#endif

#if !BUILDFLAG(IS_ANDROID)
#include "brave/browser/ai_chat/tools/index_local_file_tool.h"
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
  if (delegate_to_subagent_tool_) {
    tool_ptrs.push_back(delegate_to_subagent_tool_->GetWeakPtr());
  }
  if (open_delegation_tool_) {
    tool_ptrs.push_back(open_delegation_tool_->GetWeakPtr());
  }
  if (get_delegation_status_tool_) {
    tool_ptrs.push_back(get_delegation_status_tool_->GetWeakPtr());
  }
  if (approve_delegation_task_tool_) {
    tool_ptrs.push_back(approve_delegation_task_tool_->GetWeakPtr());
  }
  if (reject_delegation_task_tool_) {
    tool_ptrs.push_back(reject_delegation_task_tool_->GetWeakPtr());
  }
  if (inject_delegation_brief_tool_) {
    tool_ptrs.push_back(inject_delegation_brief_tool_->GetWeakPtr());
  }
  if (create_delegation_task_tool_) {
    tool_ptrs.push_back(create_delegation_task_tool_->GetWeakPtr());
  }
  if (get_desktop_screenshot_tool_) {
    tool_ptrs.push_back(get_desktop_screenshot_tool_->GetWeakPtr());
  }
#if BUILDFLAG(IS_WIN)
  if (desktop_move_mouse_tool_) {
    tool_ptrs.push_back(desktop_move_mouse_tool_->GetWeakPtr());
  }
  if (desktop_click_tool_) {
    tool_ptrs.push_back(desktop_click_tool_->GetWeakPtr());
  }
  if (desktop_scroll_tool_) {
    tool_ptrs.push_back(desktop_scroll_tool_->GetWeakPtr());
  }
  if (desktop_type_text_tool_) {
    tool_ptrs.push_back(desktop_type_text_tool_->GetWeakPtr());
  }
  if (desktop_press_key_tool_) {
    tool_ptrs.push_back(desktop_press_key_tool_->GetWeakPtr());
  }
  if (open_rdp_session_tool_) {
    tool_ptrs.push_back(open_rdp_session_tool_->GetWeakPtr());
  }
  if (close_rdp_session_tool_) {
    tool_ptrs.push_back(close_rdp_session_tool_->GetWeakPtr());
  }
#endif
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
  if (index_bookmarks_tool_) {
    tool_ptrs.push_back(index_bookmarks_tool_->GetWeakPtr());
  }
#if !BUILDFLAG(IS_ANDROID)
  if (index_local_file_tool_) {
    tool_ptrs.push_back(index_local_file_tool_->GetWeakPtr());
  }
#endif
  if (open_n8n_tool_) {
    tool_ptrs.push_back(open_n8n_tool_->GetWeakPtr());
  }
  if (set_n8n_api_key_tool_) {
    tool_ptrs.push_back(set_n8n_api_key_tool_->GetWeakPtr());
  }
  if (create_n8n_workflow_tool_) {
    tool_ptrs.push_back(create_n8n_workflow_tool_->GetWeakPtr());
  }
  if (run_n8n_workflow_tool_) {
    tool_ptrs.push_back(run_n8n_workflow_tool_->GetWeakPtr());
  }
  if (update_n8n_workflow_tool_) {
    tool_ptrs.push_back(update_n8n_workflow_tool_->GetWeakPtr());
  }
  if (list_n8n_workflow_versions_tool_) {
    tool_ptrs.push_back(list_n8n_workflow_versions_tool_->GetWeakPtr());
  }
  if (rollback_n8n_workflow_tool_) {
    tool_ptrs.push_back(rollback_n8n_workflow_tool_->GetWeakPtr());
  }
  if (list_n8n_mcp_tools_tool_) {
    tool_ptrs.push_back(list_n8n_mcp_tools_tool_->GetWeakPtr());
  }
  if (call_n8n_mcp_tool_tool_) {
    tool_ptrs.push_back(call_n8n_mcp_tool_tool_->GetWeakPtr());
  }
  if (export_n8n_backup_tool_) {
    tool_ptrs.push_back(export_n8n_backup_tool_->GetWeakPtr());
  }
  if (import_n8n_backup_tool_) {
    tool_ptrs.push_back(import_n8n_backup_tool_->GetWeakPtr());
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
  delegate_to_subagent_tool_ =
      std::make_unique<DelegateToSubagentTool>(browser_context);
  auto* delegation_manager =
      DelegationProcessManagerFactory::GetForBrowserContext(browser_context);
  open_delegation_tool_ =
      std::make_unique<OpenDelegationTool>(delegation_manager, browser_context);
  get_delegation_status_tool_ =
      std::make_unique<GetDelegationStatusTool>(delegation_manager);
  approve_delegation_task_tool_ =
      std::make_unique<ApproveDelegationTaskTool>(delegation_manager);
  reject_delegation_task_tool_ =
      std::make_unique<RejectDelegationTaskTool>(delegation_manager);
  inject_delegation_brief_tool_ =
      std::make_unique<InjectDelegationBriefTool>(delegation_manager);
  create_delegation_task_tool_ =
      std::make_unique<CreateDelegationTaskTool>(delegation_manager);
  get_desktop_screenshot_tool_ =
      std::make_unique<GetDesktopScreenshotTool>(browser_context);
#if BUILDFLAG(IS_WIN)
  desktop_move_mouse_tool_ =
      std::make_unique<DesktopMoveMouseTool>(browser_context);
  desktop_click_tool_ = std::make_unique<DesktopClickTool>(browser_context);
  desktop_scroll_tool_ = std::make_unique<DesktopScrollTool>(browser_context);
  desktop_type_text_tool_ =
      std::make_unique<DesktopTypeTextTool>(browser_context);
  desktop_press_key_tool_ =
      std::make_unique<DesktopPressKeyTool>(browser_context);
  open_rdp_session_tool_ =
      std::make_unique<OpenRdpSessionTool>(browser_context);
  close_rdp_session_tool_ =
      std::make_unique<CloseRdpSessionTool>(browser_context);
#endif
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
  index_bookmarks_tool_ = std::make_unique<IndexBookmarksTool>(
      AiChatContentIndexFactory::GetForBrowserContext(browser_context),
      profile_);
#if !BUILDFLAG(IS_ANDROID)
  index_local_file_tool_ =
      std::make_unique<IndexLocalFileTool>(browser_context);
#endif
  auto* n8n_manager =
      N8nProcessManagerFactory::GetForBrowserContext(browser_context);
  open_n8n_tool_ =
      std::make_unique<OpenN8nTool>(n8n_manager, browser_context);
  set_n8n_api_key_tool_ =
      std::make_unique<SetN8nApiKeyTool>(browser_context);
  create_n8n_workflow_tool_ = std::make_unique<CreateN8nWorkflowTool>(
      n8n_manager, browser_context);
  run_n8n_workflow_tool_ =
      std::make_unique<RunN8nWorkflowTool>(n8n_manager, browser_context);
  update_n8n_workflow_tool_ = std::make_unique<UpdateN8nWorkflowTool>(
      n8n_manager, browser_context);
  list_n8n_workflow_versions_tool_ =
      std::make_unique<ListN8nWorkflowVersionsTool>(n8n_manager,
                                                     browser_context);
  rollback_n8n_workflow_tool_ = std::make_unique<RollbackN8nWorkflowTool>(
      n8n_manager, browser_context);
  list_n8n_mcp_tools_tool_ = std::make_unique<ListN8nMcpToolsTool>(
      n8n_manager, browser_context);
  call_n8n_mcp_tool_tool_ = std::make_unique<CallN8nMcpToolTool>(
      n8n_manager, browser_context);
  export_n8n_backup_tool_ =
      std::make_unique<ExportN8nBackupTool>(n8n_manager, browser_context);
  import_n8n_backup_tool_ =
      std::make_unique<ImportN8nBackupTool>(n8n_manager, browser_context);
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

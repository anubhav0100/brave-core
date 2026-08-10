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
#include "build/build_config.h"
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
class ApproveDelegationTaskTool;
class CreateDelegationTaskTool;
class CreateWordDocumentTool;
class DelegateToSubagentTool;
class DelegationProcessManager;
class GetDelegationStatusTool;
class GetDesktopScreenshotTool;
#if BUILDFLAG(IS_WIN)
class DesktopClickTool;
class DesktopMoveMouseTool;
class DesktopPressKeyTool;
class DesktopScrollTool;
class DesktopTypeTextTool;
#endif
class InjectDelegationBriefTool;
class OpenDelegationTool;
class ReadWordDocumentTool;
class RejectDelegationTaskTool;
class CallN8nMcpToolTool;
class CreateN8nWorkflowTool;
class ExportN8nBackupTool;
class ImportN8nBackupTool;
class HistorySearchTool;
class IndexBookmarksTool;
#if !BUILDFLAG(IS_ANDROID)
class IndexLocalFileTool;
#endif
class ClearResponseMemoryTool;
class ListN8nMcpToolsTool;
class ListN8nWorkflowVersionsTool;
class N8nProcessManager;
class OpenN8nTool;
class PageCaptureSession;
class ResponseMemorySession;
class RollbackN8nWorkflowTool;
class RunN8nWorkflowTool;
class SetN8nApiKeyTool;
class UpdateN8nWorkflowTool;
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
  std::unique_ptr<IndexBookmarksTool> index_bookmarks_tool_;
#if !BUILDFLAG(IS_ANDROID)
  std::unique_ptr<IndexLocalFileTool> index_local_file_tool_;
#endif
  std::unique_ptr<OpenN8nTool> open_n8n_tool_;
  std::unique_ptr<SetN8nApiKeyTool> set_n8n_api_key_tool_;
  std::unique_ptr<CreateN8nWorkflowTool> create_n8n_workflow_tool_;
  std::unique_ptr<RunN8nWorkflowTool> run_n8n_workflow_tool_;
  std::unique_ptr<UpdateN8nWorkflowTool> update_n8n_workflow_tool_;
  std::unique_ptr<ListN8nWorkflowVersionsTool>
      list_n8n_workflow_versions_tool_;
  std::unique_ptr<RollbackN8nWorkflowTool> rollback_n8n_workflow_tool_;
  std::unique_ptr<ListN8nMcpToolsTool> list_n8n_mcp_tools_tool_;
  std::unique_ptr<CallN8nMcpToolTool> call_n8n_mcp_tool_tool_;
  std::unique_ptr<ExportN8nBackupTool> export_n8n_backup_tool_;
  std::unique_ptr<ImportN8nBackupTool> import_n8n_backup_tool_;
  std::unique_ptr<RunWorkflowTool> run_workflow_tool_;
  std::unique_ptr<CreateSpreadsheetTool> create_spreadsheet_tool_;
  std::unique_ptr<CreatePresentationTool> create_presentation_tool_;
#if BUILDFLAG(ENABLE_PRINTING)
  std::unique_ptr<CreatePdfDocumentTool> create_pdf_document_tool_;
#endif
  std::unique_ptr<HistorySearchTool> history_search_tool_;
  std::unique_ptr<DelegateToSubagentTool> delegate_to_subagent_tool_;
  std::unique_ptr<OpenDelegationTool> open_delegation_tool_;
  std::unique_ptr<GetDelegationStatusTool> get_delegation_status_tool_;
  std::unique_ptr<ApproveDelegationTaskTool> approve_delegation_task_tool_;
  std::unique_ptr<RejectDelegationTaskTool> reject_delegation_task_tool_;
  std::unique_ptr<InjectDelegationBriefTool> inject_delegation_brief_tool_;
  std::unique_ptr<CreateDelegationTaskTool> create_delegation_task_tool_;
  std::unique_ptr<GetDesktopScreenshotTool> get_desktop_screenshot_tool_;
#if BUILDFLAG(IS_WIN)
  std::unique_ptr<DesktopMoveMouseTool> desktop_move_mouse_tool_;
  std::unique_ptr<DesktopClickTool> desktop_click_tool_;
  std::unique_ptr<DesktopScrollTool> desktop_scroll_tool_;
  std::unique_ptr<DesktopTypeTextTool> desktop_type_text_tool_;
  std::unique_ptr<DesktopPressKeyTool> desktop_press_key_tool_;
#endif
#if BUILDFLAG(ENABLE_AI_CHAT_TAB_MANAGEMENT_TOOL)
  std::unique_ptr<TabManagementTool> tab_management_tool_;
#endif
  raw_ptr<Profile> profile_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_BROWSER_TOOL_PROVIDER_H_

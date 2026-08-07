// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_RUN_WORKFLOW_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_RUN_WORKFLOW_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/workflows/workflow_runtime.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Exposes workflow_orchestration_engine.md section 54's `run_workflow` tool:
// lets the assistant run a workflow saved via the "Workflows" Settings
// section (see workflow_repository.h) against the current active tab, and
// reports back whether it completed, failed, or hit an unimplemented step
// type - the full nested-flow/loop/approval engine described in that
// document is not implemented yet (see workflow_runtime.h), so this only
// runs the flat, single-workflow subset it supports.
class RunWorkflowTool : public Tool {
 public:
  explicit RunWorkflowTool(content::BrowserContext* browser_context);
  ~RunWorkflowTool() override;

  RunWorkflowTool(const RunWorkflowTool&) = delete;
  RunWorkflowTool& operator=(const RunWorkflowTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnRunComplete(UseToolCallback callback,
                     WorkflowRuntime::ExecutionResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<RunWorkflowTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_RUN_WORKFLOW_TOOL_H_

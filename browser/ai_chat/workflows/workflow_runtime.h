// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_RUNTIME_H_
#define BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_RUNTIME_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "brave/browser/ai_chat/workflows/workflow_definition.h"
#include "brave/browser/ai_chat/workflows/workflow_variable_store.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace ai_chat {

// Executes a WorkflowDefinition against a real tab - Phase 3 ("Basic
// Runtime") of the workflow orchestration engine design doc, implementing
// exactly the simplified algorithm in that document's "Execution Algorithm"
// section for the node types workflow_definition.h parses.
//
// Deliberately NOT implemented yet (later phases in the document's own
// plan - see workflow_definition.h's WorkflowStepTypeFromString, which
// rejects these at parse time so a workflow using them fails validation
// clearly rather than silently doing the wrong thing):
// - call_flow (nested/reusable workflows) and its call stack.
// - for_each/while/until loops.
// - ai.decide/ai.action/ai.extract nodes (agentic steps - this runtime is
//   the deterministic half of the design doc's section 83 split; an AI
//   node would need to hand off to the existing ContentAgentToolProvider
//   machinery, which is out of scope for this first runtime).
// - tool.call/webhook.call nodes (though the existing browser/webhook tool
//   infrastructure this session already built could back these directly).
// - approval/user_input pause-and-resume nodes.
// - retries, checkpoints/resume, and versioned dependency resolution.
class WorkflowRuntime {
 public:
  struct ExecutionResult {
    ExecutionResult();
    ExecutionResult(const ExecutionResult&);
    ExecutionResult& operator=(const ExecutionResult&);
    ~ExecutionResult();

    bool success = false;
    // Set when !success.
    std::string error_message;
    // Set when a `fail` step was reached (a subset of !success).
    std::optional<std::string> fail_reason;
    std::map<std::string, std::string> outputs;
    std::vector<std::string> executed_step_ids;
  };
  using ResultCallback = base::OnceCallback<void(ExecutionResult)>;

  // Runs `definition` starting at its single `start` step, using `inputs`
  // for "${input.*}" references, driving `web_contents` for browser.*
  // steps. `web_contents` must outlive the run. Self-deleting - the caller
  // doesn't need to hold onto anything; `callback` fires exactly once.
  static void Start(WorkflowDefinition definition,
                    base::DictValue inputs,
                    content::WebContents* web_contents,
                    ResultCallback callback);

 private:
  WorkflowRuntime(WorkflowDefinition definition,
                  base::DictValue inputs,
                  content::WebContents* web_contents,
                  ResultCallback callback);
  ~WorkflowRuntime();

  WorkflowRuntime(const WorkflowRuntime&) = delete;
  WorkflowRuntime& operator=(const WorkflowRuntime&) = delete;

  void Run();
  void ExecuteStep(const std::string& step_id);
  const WorkflowStep* FindStep(const std::string& step_id) const;

  void ExecuteBrowserNavigate(const WorkflowStep& step);
  void OnNavigateResult(std::string next_step_id, bool success);

  void ExecuteBrowserClickOrType(const WorkflowStep& step);
  void OnScriptResult(std::string next_step_id, base::Value result);

  void ExecuteBrowserWait(const WorkflowStep& step);
  void OnWaitComplete(std::string next_step_id);

  void Finish(ExecutionResult result);

  class NavigationWaiter;

  WorkflowDefinition definition_;
  WorkflowVariableStore variable_store_;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  ResultCallback callback_;
  ExecutionResult result_;
  int steps_executed_ = 0;

  std::unique_ptr<NavigationWaiter> navigation_waiter_;
  base::OneShotTimer wait_timer_;

  base::WeakPtrFactory<WorkflowRuntime> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_RUNTIME_H_

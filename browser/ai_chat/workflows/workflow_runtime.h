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
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "brave/browser/ai_chat/workflows/workflow_definition.h"
#include "brave/browser/ai_chat/workflows/workflow_variable_store.h"
#include "brave/components/ai_chat/core/browser/engine/engine_consumer.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace ai_chat {

class ModelService;
class WorkflowRepository;

// Executes a WorkflowDefinition against a real tab - Phase 3 ("Basic
// Runtime") of the workflow orchestration engine design doc, extended by
// Phase 4 (call_flow), Phase 5 (loops), and Phase 6 (bounded AI nodes) - see
// workflow_definition.h for the full node-type list each phase added.
//
// Deliberately NOT implemented (later phases in the document's own plan -
// see workflow_definition.h's WorkflowStepTypeFromString, which rejects
// these at parse time so a workflow using them fails validation clearly
// rather than silently doing the wrong thing):
// - ai.action (the open-ended bounded-autonomy node - repeatedly asking the
//   model "what one action next" against a whitelist until a completion
//   condition is met). Deferred as its own follow-up: it's a materially
//   different, riskier shape than ai.extract/ai.decide's single bounded
//   model call - a real agentic loop with its own step-count/time budget
//   and action-whitelist enforcement - and deserves its own focused design
//   pass rather than being rushed alongside four other new node families
//   at once.
// - tool.call/webhook.call nodes (though the existing browser/webhook tool
//   infrastructure this session already built could back these directly).
// - approval/user_input pause-and-resume nodes.
// - retries, checkpoints/resume, and true multi-version dependency
//   resolution (call_flow resolves the child by id only, from whichever
//   single copy WorkflowRepository currently has stored for that id - see
//   its own file comment for why full versioning is out of scope).
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
  // steps. `repository` is used to resolve call_flow steps' child
  // workflows by id - may be null if `definition` is known not to use
  // call_flow (a null repository with a call_flow step present fails that
  // step with a clear error rather than crashing). `web_contents` must
  // outlive the run. Self-deleting - the caller doesn't need to hold onto
  // anything; `callback` fires exactly once.
  static void Start(WorkflowDefinition definition,
                    base::DictValue inputs,
                    content::WebContents* web_contents,
                    WorkflowRepository* repository,
                    ResultCallback callback);

 private:
  // The doc's runtime_limits example default for max_flow_call_depth.
  static constexpr int kMaxCallDepth = 10;

  // Used by ExecuteCallFlow to start a child run with the parent's call
  // stack context, so depth/cycle limits apply across the whole chain, not
  // just within one workflow.
  static void StartNested(WorkflowDefinition definition,
                          base::DictValue inputs,
                          content::WebContents* web_contents,
                          WorkflowRepository* repository,
                          int call_depth,
                          std::vector<std::string> ancestor_flow_ids,
                          ResultCallback callback);

  WorkflowRuntime(WorkflowDefinition definition,
                  base::DictValue inputs,
                  content::WebContents* web_contents,
                  WorkflowRepository* repository,
                  int call_depth,
                  std::vector<std::string> ancestor_flow_ids,
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

  // Phase 4 - call_flow.
  void ExecuteCallFlow(const WorkflowStep& step);
  void OnChildFlowResult(std::string step_id,
                         std::string next_step_id,
                         std::string on_child_failure,
                         std::map<std::string, std::string> call_outputs,
                         ExecutionResult child_result);

  // Phase 5 - loops. for_each and while/until share the same active-loop
  // stack and break/continue handling but have different re-entry
  // semantics (item-list vs. condition), so they're separate entry points.
  // Move-only: `items` holds base::Value elements, which aren't copyable.
  struct LoopContext {
    LoopContext();
    LoopContext(const LoopContext&) = delete;
    LoopContext& operator=(const LoopContext&) = delete;
    LoopContext(LoopContext&&);
    LoopContext& operator=(LoopContext&&);
    ~LoopContext();

    std::string loop_step_id;
    std::string after_loop_next;
    bool is_for_each = false;
    // for_each only.
    std::string item_variable;
    std::string index_variable;
    std::vector<base::Value> items;
    // while/until only.
    bool is_until = false;
    std::string loop_condition;

    int index = 0;
    int max_iterations = 0;
    base::TimeTicks start_time;
  };
  void ExecuteForEach(const WorkflowStep& step);
  void ExecuteLoopCondition(const WorkflowStep& step);
  void ExecuteBreak();
  void ExecuteContinue();

  // Phase 6 - bounded AI nodes. Both funnel through RunAiStep(), which is
  // the only place this runtime talks to a model - a single one-shot
  // GenerateAssistantResponse call per step, the same pattern
  // DelegateToSubagentTool uses (no tools, no conversation history beyond
  // this one instruction, is_temporary_chat=true).
  void ExecuteAiExtract(const WorkflowStep& step);
  void OnAiExtractResult(std::string step_id,
                         std::string output_variable,
                         std::string next_step_id,
                         bool success,
                         std::string text);
  void ExecuteAiDecide(const WorkflowStep& step);
  void OnAiDecideResult(std::string step_id,
                        std::string output_variable,
                        std::vector<std::string> allowed_outcomes,
                        std::string next_step_id,
                        bool success,
                        std::string text);

  struct AiStepState {
    AiStepState();
    ~AiStepState();

    std::unique_ptr<EngineConsumer> engine;
    std::string completion_text;
    base::OnceCallback<void(bool, std::string)> callback;
  };
  using AiStepCallback = base::OnceCallback<void(bool success, std::string text)>;
  void RunAiStep(const std::string& prompt, AiStepCallback callback);
  void OnAiStepDataReceived(std::shared_ptr<AiStepState> state,
                            EngineConsumer::GenerationResultData result);
  void OnAiStepCompleted(std::shared_ptr<AiStepState> state,
                         EngineConsumer::GenerationResult result);

  void Finish(ExecutionResult result);

  class NavigationWaiter;

  WorkflowDefinition definition_;
  WorkflowVariableStore variable_store_;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  raw_ptr<WorkflowRepository> repository_ = nullptr;
  int call_depth_ = 0;
  std::vector<std::string> ancestor_flow_ids_;
  ResultCallback callback_;
  ExecutionResult result_;
  int steps_executed_ = 0;

  std::vector<LoopContext> loop_stack_;

  raw_ptr<ModelService> model_service_ = nullptr;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  std::unique_ptr<NavigationWaiter> navigation_waiter_;
  base::OneShotTimer wait_timer_;

  base::WeakPtrFactory<WorkflowRuntime> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_RUNTIME_H_

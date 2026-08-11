// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_runtime.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "brave/browser/ai_chat/model_service_factory.h"
#include "brave/browser/ai_chat/workflows/workflow_repository.h"
#include "brave/components/ai_chat/core/browser/model_service.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {
constexpr int kMaxSteps = 200;  // Matches the design doc's example runtime_limits.
constexpr base::TimeDelta kNavigationTimeout = base::Seconds(20);

std::string ValueToVariableText(const base::Value& value) {
  if (value.is_string()) {
    return value.GetString();
  }
  std::string serialized;
  base::JSONWriter::Write(value, &serialized);
  return serialized;
}

// Best-effort strips a leading/trailing ```json ... ``` or ``` ... ``` fence
// some models wrap structured output in despite being asked not to.
std::string StripCodeFences(const std::string& text) {
  std::string trimmed;
  base::TrimWhitespaceASCII(text, base::TRIM_ALL, &trimmed);
  if (!base::StartsWith(trimmed, "```")) {
    return trimmed;
  }
  size_t first_newline = trimmed.find('\n');
  if (first_newline == std::string::npos) {
    return trimmed;
  }
  std::string body = trimmed.substr(first_newline + 1);
  size_t closing = body.rfind("```");
  if (closing != std::string::npos) {
    body = body.substr(0, closing);
  }
  base::TrimWhitespaceASCII(body, base::TRIM_ALL, &trimmed);
  return trimmed;
}

}  // namespace

WorkflowRuntime::ExecutionResult::ExecutionResult() = default;
WorkflowRuntime::ExecutionResult::ExecutionResult(const ExecutionResult&) =
    default;
WorkflowRuntime::ExecutionResult& WorkflowRuntime::ExecutionResult::operator=(
    const ExecutionResult&) = default;
WorkflowRuntime::ExecutionResult::~ExecutionResult() = default;

WorkflowRuntime::LoopContext::LoopContext() = default;
WorkflowRuntime::LoopContext::LoopContext(LoopContext&&) = default;
WorkflowRuntime::LoopContext& WorkflowRuntime::LoopContext::operator=(
    LoopContext&&) = default;
WorkflowRuntime::LoopContext::~LoopContext() = default;

WorkflowRuntime::AiStepState::AiStepState() = default;
WorkflowRuntime::AiStepState::~AiStepState() = default;

// Waits for one navigation to finish in the primary main frame, then
// reports success/failure exactly once. Self-contained so WorkflowRuntime
// can drop it as soon as it fires (or on timeout).
class WorkflowRuntime::NavigationWaiter : public content::WebContentsObserver {
 public:
  NavigationWaiter(content::WebContents* web_contents,
                   base::OnceCallback<void(bool)> callback)
      : content::WebContentsObserver(web_contents),
        callback_(std::move(callback)) {}

  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (done_ || !handle->IsInPrimaryMainFrame()) {
      return;
    }
    done_ = true;
    std::move(callback_).Run(handle->HasCommitted() && !handle->IsErrorPage());
  }

  void RunIfNotDone(bool success) {
    if (done_) {
      return;
    }
    done_ = true;
    std::move(callback_).Run(success);
  }

 private:
  bool done_ = false;
  base::OnceCallback<void(bool)> callback_;
};

// static
void WorkflowRuntime::Start(WorkflowDefinition definition,
                            base::DictValue inputs,
                            content::WebContents* web_contents,
                            WorkflowRepository* repository,
                            ResultCallback callback) {
  StartNested(std::move(definition), std::move(inputs), web_contents,
             repository, /*call_depth=*/0, /*ancestor_flow_ids=*/{},
             std::move(callback));
}

// static
void WorkflowRuntime::StartNested(WorkflowDefinition definition,
                                  base::DictValue inputs,
                                  content::WebContents* web_contents,
                                  WorkflowRepository* repository,
                                  int call_depth,
                                  std::vector<std::string> ancestor_flow_ids,
                                  ResultCallback callback) {
  auto* runtime = new WorkflowRuntime(
      std::move(definition), std::move(inputs), web_contents, repository,
      call_depth, std::move(ancestor_flow_ids), std::move(callback));
  runtime->Run();
}

WorkflowRuntime::WorkflowRuntime(WorkflowDefinition definition,
                                 base::DictValue inputs,
                                 content::WebContents* web_contents,
                                 WorkflowRepository* repository,
                                 int call_depth,
                                 std::vector<std::string> ancestor_flow_ids,
                                 ResultCallback callback)
    : definition_(std::move(definition)),
      web_contents_(web_contents),
      repository_(repository),
      call_depth_(call_depth),
      ancestor_flow_ids_(std::move(ancestor_flow_ids)),
      callback_(std::move(callback)) {
  for (auto [name, value] : inputs) {
    variable_store_.SetInput(name, std::move(value));
  }
  for (const auto& [name, value_expr] : definition_.initial_variables) {
    variable_store_.SetVariable(name, variable_store_.Resolve(value_expr));
  }

  if (web_contents_) {
    if (content::BrowserContext* browser_context =
            web_contents_->GetBrowserContext()) {
      model_service_ = ModelServiceFactory::GetForBrowserContext(browser_context);
      url_loader_factory_ = browser_context->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
    }
  }
}

WorkflowRuntime::~WorkflowRuntime() = default;

const WorkflowStep* WorkflowRuntime::FindStep(
    const std::string& step_id) const {
  for (const auto& step : definition_.steps) {
    if (step.id == step_id) {
      return &step;
    }
  }
  return nullptr;
}

void WorkflowRuntime::Run() {
  const WorkflowStep* start_step = nullptr;
  for (const auto& step : definition_.steps) {
    if (step.type == WorkflowStepType::kStart) {
      start_step = &step;
      break;
    }
  }
  if (!start_step) {
    ExecutionResult result;
    result.error_message = "Workflow has no 'start' step.";
    Finish(std::move(result));
    return;
  }
  ExecuteStep(start_step->id);
}

void WorkflowRuntime::ExecuteStep(const std::string& step_id) {
  if (++steps_executed_ > kMaxSteps) {
    ExecutionResult result;
    result.error_message = base::StrCat(
        {"Workflow exceeded the maximum of ", base::NumberToString(kMaxSteps),
         " steps - stopping to avoid an infinite loop."});
    Finish(std::move(result));
    return;
  }

  // Keep "${loop.item}"/"${loop.index}" in sync with whichever loop is
  // currently innermost, including correctly reverting once an inner loop
  // pops back to an outer one (or to no loop at all) - see
  // workflow_variable_store.h's file comment.
  if (!loop_stack_.empty()) {
    const LoopContext& ctx = loop_stack_.back();
    if (ctx.is_for_each && ctx.index >= 0 &&
        static_cast<size_t>(ctx.index) < ctx.items.size()) {
      variable_store_.SetLoopItem(ctx.items[ctx.index].Clone());
    }
    variable_store_.SetLoopIndex(ctx.index);
  } else {
    variable_store_.ClearLoopBindings();
  }

  const WorkflowStep* step = FindStep(step_id);
  if (!step) {
    ExecutionResult result = result_;
    result.error_message =
        base::StrCat({"Workflow referenced unknown step id '", step_id, "'"});
    Finish(std::move(result));
    return;
  }
  result_.executed_step_ids.push_back(step->id);

  switch (step->type) {
    case WorkflowStepType::kStart:
      ExecuteStep(step->next);
      return;

    case WorkflowStepType::kSetVariable:
      variable_store_.SetVariable(
          step->variable_name, variable_store_.Resolve(step->variable_value));
      ExecuteStep(step->next);
      return;

    case WorkflowStepType::kCondition: {
      bool condition_result =
          variable_store_.EvaluateCondition(step->condition_expression);
      ExecuteStep(condition_result ? step->on_true : step->on_false);
      return;
    }

    case WorkflowStepType::kBrowserNavigate:
      ExecuteBrowserNavigate(*step);
      return;

    case WorkflowStepType::kBrowserClick:
    case WorkflowStepType::kBrowserType:
      ExecuteBrowserClickOrType(*step);
      return;

    case WorkflowStepType::kBrowserWait:
      ExecuteBrowserWait(*step);
      return;

    case WorkflowStepType::kCallFlow:
      ExecuteCallFlow(*step);
      return;

    case WorkflowStepType::kForEach:
      ExecuteForEach(*step);
      return;

    case WorkflowStepType::kWhile:
    case WorkflowStepType::kUntil:
      ExecuteLoopCondition(*step);
      return;

    case WorkflowStepType::kBreak:
      ExecuteBreak();
      return;

    case WorkflowStepType::kContinue:
      ExecuteContinue();
      return;

    case WorkflowStepType::kAiExtract:
      ExecuteAiExtract(*step);
      return;

    case WorkflowStepType::kAiDecide:
      ExecuteAiDecide(*step);
      return;

    case WorkflowStepType::kFail: {
      ExecutionResult result = result_;
      result.success = false;
      result.fail_reason = step->fail_reason;
      result.error_message = step->fail_reason.empty()
                                 ? "Workflow reached a 'fail' step."
                                 : step->fail_reason;
      Finish(std::move(result));
      return;
    }

    case WorkflowStepType::kComplete: {
      ExecutionResult result = result_;
      result.success = true;
      for (const auto& [name, expr] : step->outputs) {
        result.outputs[name] = variable_store_.Resolve(expr);
      }
      Finish(std::move(result));
      return;
    }
  }
}

void WorkflowRuntime::ExecuteBrowserNavigate(const WorkflowStep& step) {
  if (!web_contents_) {
    ExecutionResult result = result_;
    result.error_message = "No active tab to navigate.";
    Finish(std::move(result));
    return;
  }
  GURL url(variable_store_.Resolve(step.url));
  if (!url.is_valid()) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat({"Invalid URL for step '", step.id, "'"});
    Finish(std::move(result));
    return;
  }

  navigation_waiter_ = std::make_unique<NavigationWaiter>(
      web_contents_.get(),
      base::BindOnce(&WorkflowRuntime::OnNavigateResult,
                     weak_ptr_factory_.GetWeakPtr(), step.next));
  wait_timer_.Start(
      FROM_HERE, kNavigationTimeout,
      base::BindOnce(&NavigationWaiter::RunIfNotDone,
                     base::Unretained(navigation_waiter_.get()), false));

  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  web_contents_->GetController().LoadURLWithParams(params);
}

void WorkflowRuntime::OnNavigateResult(std::string next_step_id,
                                       bool success) {
  wait_timer_.Stop();
  navigation_waiter_.reset();
  if (!success) {
    ExecutionResult result = result_;
    result.error_message = "Navigation failed or timed out.";
    Finish(std::move(result));
    return;
  }
  ExecuteStep(next_step_id);
}

void WorkflowRuntime::ExecuteBrowserClickOrType(const WorkflowStep& step) {
  if (!web_contents_) {
    ExecutionResult result = result_;
    result.error_message = "No active tab to act on.";
    Finish(std::move(result));
    return;
  }

  std::string selector_json;
  base::JSONWriter::Write(base::Value(step.selector), &selector_json);

  std::string script;
  if (step.type == WorkflowStepType::kBrowserClick) {
    script = base::StrCat(
        {"(function(){var el=document.querySelector(", selector_json,
         ");if(!el)return 'not_found';el.click();return 'ok';})()"});
  } else {
    std::string text_json;
    base::JSONWriter::Write(
        base::Value(variable_store_.Resolve(step.text)), &text_json);
    script = base::StrCat(
        {"(function(){var el=document.querySelector(", selector_json,
         ");if(!el)return 'not_found';el.focus();el.value=", text_json,
         ";el.dispatchEvent(new Event('input',{bubbles:true}));"
         "el.dispatchEvent(new Event('change',{bubbles:true}));"
         "return 'ok';})()"});
  }

  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::BindOnce(&WorkflowRuntime::OnScriptResult,
                     weak_ptr_factory_.GetWeakPtr(), step.next),
      ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

void WorkflowRuntime::OnScriptResult(std::string next_step_id,
                                     base::Value result_value) {
  if (result_value.is_string() && result_value.GetString() == "not_found") {
    ExecutionResult result = result_;
    result.error_message = "Element not found for the step's selector.";
    Finish(std::move(result));
    return;
  }
  ExecuteStep(next_step_id);
}

void WorkflowRuntime::ExecuteBrowserWait(const WorkflowStep& step) {
  wait_timer_.Start(FROM_HERE, base::Seconds(step.wait_seconds),
                    base::BindOnce(&WorkflowRuntime::OnWaitComplete,
                                   weak_ptr_factory_.GetWeakPtr(), step.next));
}

void WorkflowRuntime::OnWaitComplete(std::string next_step_id) {
  ExecuteStep(next_step_id);
}

// ---------------------------------------------------------------------
// Phase 4 - call_flow
// ---------------------------------------------------------------------

void WorkflowRuntime::ExecuteCallFlow(const WorkflowStep& step) {
  if (!repository_) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"call_flow step '", step.id,
         "' can't resolve child workflows - no repository available."});
    Finish(std::move(result));
    return;
  }
  if (std::ranges::find(ancestor_flow_ids_, step.flow_id) !=
          ancestor_flow_ids_.end() ||
      step.flow_id == definition_.id) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"call_flow step '", step.id, "' would create a cycle: '",
         step.flow_id, "' is already an ancestor of this call chain."});
    Finish(std::move(result));
    return;
  }
  if (call_depth_ + 1 > kMaxCallDepth) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"call_flow step '", step.id, "' exceeded the maximum call depth of ",
         base::NumberToString(kMaxCallDepth), "."});
    Finish(std::move(result));
    return;
  }

  std::optional<WorkflowDefinition> child_definition =
      repository_->GetWorkflow(step.flow_id);
  if (!child_definition) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"call_flow step '", step.id, "' references unknown workflow id '",
         step.flow_id, "'"});
    Finish(std::move(result));
    return;
  }

  base::DictValue child_inputs;
  for (const auto& [child_input_name, parent_expr] : step.call_inputs) {
    child_inputs.Set(child_input_name, variable_store_.Resolve(parent_expr));
  }

  std::vector<std::string> child_ancestors = ancestor_flow_ids_;
  child_ancestors.push_back(definition_.id);

  StartNested(
      std::move(*child_definition), std::move(child_inputs), web_contents_,
      repository_, call_depth_ + 1, std::move(child_ancestors),
      base::BindOnce(&WorkflowRuntime::OnChildFlowResult,
                     weak_ptr_factory_.GetWeakPtr(), step.id, step.next,
                     step.on_child_failure, step.call_outputs));
}

void WorkflowRuntime::OnChildFlowResult(
    std::string step_id,
    std::string next_step_id,
    std::string on_child_failure,
    std::map<std::string, std::string> call_outputs,
    ExecutionResult child_result) {
  if (!child_result.success) {
    if (on_child_failure == "continue") {
      ExecuteStep(next_step_id);
      return;
    }
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"call_flow step '", step_id, "' failed: ", child_result.error_message});
    Finish(std::move(result));
    return;
  }

  base::DictValue all_outputs;
  for (const auto& [name, value] : child_result.outputs) {
    all_outputs.Set(name, value);
  }
  variable_store_.SetStepOutput(step_id, base::Value(std::move(all_outputs)));

  for (const auto& [child_output_name, parent_var_name] : call_outputs) {
    auto it = child_result.outputs.find(child_output_name);
    if (it != child_result.outputs.end()) {
      variable_store_.SetVariable(parent_var_name, it->second);
    }
  }
  ExecuteStep(next_step_id);
}

// ---------------------------------------------------------------------
// Phase 5 - loops
// ---------------------------------------------------------------------

void WorkflowRuntime::ExecuteForEach(const WorkflowStep& step) {
  if (!loop_stack_.empty() && loop_stack_.back().loop_step_id == step.id &&
      loop_stack_.back().is_for_each) {
    LoopContext& ctx = loop_stack_.back();
    ctx.index++;
    if (ctx.index >= static_cast<int>(ctx.items.size()) ||
        ctx.index >= ctx.max_iterations) {
      std::string after_next = ctx.after_loop_next;
      loop_stack_.pop_back();
      ExecuteStep(after_next);
      return;
    }
    variable_store_.SetVariable(ctx.item_variable,
                                ValueToVariableText(ctx.items[ctx.index]));
    if (!ctx.index_variable.empty()) {
      variable_store_.SetVariable(ctx.index_variable,
                                  base::NumberToString(ctx.index));
    }
    ExecuteStep(step.body_start);
    return;
  }

  std::string items_json = variable_store_.Resolve(step.items_expression);
  std::optional<base::Value> parsed = base::JSONReader::Read(
      items_json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_list()) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"for_each step '", step.id,
         "'s 'items' did not resolve to a JSON array (got: ", items_json,
         ")"});
    Finish(std::move(result));
    return;
  }

  LoopContext ctx;
  ctx.loop_step_id = step.id;
  ctx.after_loop_next = step.next;
  ctx.is_for_each = true;
  ctx.item_variable = step.item_variable;
  ctx.index_variable = step.index_variable;
  for (const auto& entry : parsed->GetList()) {
    ctx.items.push_back(entry.Clone());
  }
  ctx.index = 0;
  ctx.max_iterations = step.max_iterations;
  ctx.start_time = base::TimeTicks::Now();

  if (ctx.items.empty()) {
    ExecuteStep(step.next);
    return;
  }

  variable_store_.SetVariable(ctx.item_variable,
                              ValueToVariableText(ctx.items[0]));
  if (!ctx.index_variable.empty()) {
    variable_store_.SetVariable(ctx.index_variable, "0");
  }
  loop_stack_.push_back(std::move(ctx));
  ExecuteStep(step.body_start);
}

void WorkflowRuntime::ExecuteLoopCondition(const WorkflowStep& step) {
  bool is_reentry = !loop_stack_.empty() &&
                    loop_stack_.back().loop_step_id == step.id &&
                    !loop_stack_.back().is_for_each;

  if (step.type == WorkflowStepType::kUntil) {
    if (is_reentry) {
      LoopContext& ctx = loop_stack_.back();
      ctx.index++;
      bool done = variable_store_.EvaluateCondition(step.loop_condition) ||
                 ctx.index >= ctx.max_iterations;
      if (done) {
        std::string after_next = ctx.after_loop_next;
        loop_stack_.pop_back();
        ExecuteStep(after_next);
        return;
      }
      ExecuteStep(step.body_start);
      return;
    }
    LoopContext ctx;
    ctx.loop_step_id = step.id;
    ctx.after_loop_next = step.next;
    ctx.is_for_each = false;
    ctx.is_until = true;
    ctx.loop_condition = step.loop_condition;
    ctx.index = 0;
    ctx.max_iterations = step.max_iterations;
    ctx.start_time = base::TimeTicks::Now();
    loop_stack_.push_back(std::move(ctx));
    ExecuteStep(step.body_start);
    return;
  }

  // "while": condition checked before each iteration, including the first.
  if (is_reentry) {
    LoopContext& ctx = loop_stack_.back();
    ctx.index++;
    if (ctx.index >= ctx.max_iterations ||
        !variable_store_.EvaluateCondition(step.loop_condition)) {
      std::string after_next = ctx.after_loop_next;
      loop_stack_.pop_back();
      ExecuteStep(after_next);
      return;
    }
    ExecuteStep(step.body_start);
    return;
  }

  if (!variable_store_.EvaluateCondition(step.loop_condition)) {
    ExecuteStep(step.next);
    return;
  }
  LoopContext ctx;
  ctx.loop_step_id = step.id;
  ctx.after_loop_next = step.next;
  ctx.is_for_each = false;
  ctx.loop_condition = step.loop_condition;
  ctx.index = 0;
  ctx.max_iterations = step.max_iterations;
  ctx.start_time = base::TimeTicks::Now();
  loop_stack_.push_back(std::move(ctx));
  ExecuteStep(step.body_start);
}

void WorkflowRuntime::ExecuteBreak() {
  if (loop_stack_.empty()) {
    ExecutionResult result = result_;
    result.error_message = "'break' step reached while not inside any loop.";
    Finish(std::move(result));
    return;
  }
  std::string after_next = loop_stack_.back().after_loop_next;
  loop_stack_.pop_back();
  ExecuteStep(after_next);
}

void WorkflowRuntime::ExecuteContinue() {
  if (loop_stack_.empty()) {
    ExecutionResult result = result_;
    result.error_message =
        "'continue' step reached while not inside any loop.";
    Finish(std::move(result));
    return;
  }
  ExecuteStep(loop_stack_.back().loop_step_id);
}

// ---------------------------------------------------------------------
// Phase 6 - bounded AI nodes
// ---------------------------------------------------------------------

void WorkflowRuntime::ExecuteAiExtract(const WorkflowStep& step) {
  std::string instruction = variable_store_.Resolve(step.ai_instruction);
  std::string prompt = base::StrCat(
      {instruction,
       "\n\nRespond with ONLY a single JSON object matching this schema, "
       "no other text, no markdown code fences:\n",
       step.ai_schema_json});
  RunAiStep(prompt, base::BindOnce(&WorkflowRuntime::OnAiExtractResult,
                                   weak_ptr_factory_.GetWeakPtr(), step.id,
                                   step.ai_output_variable, step.next));
}

void WorkflowRuntime::OnAiExtractResult(std::string step_id,
                                        std::string output_variable,
                                        std::string next_step_id,
                                        bool success,
                                        std::string text) {
  if (!success) {
    ExecutionResult result = result_;
    result.error_message =
        base::StrCat({"ai.extract step '", step_id, "' failed: ", text});
    Finish(std::move(result));
    return;
  }
  std::string cleaned = StripCodeFences(text);
  std::optional<base::Value> parsed = base::JSONReader::Read(
      cleaned, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"ai.extract step '", step_id,
         "' did not return valid JSON: ", cleaned});
    Finish(std::move(result));
    return;
  }
  variable_store_.SetVariable(output_variable, cleaned);
  variable_store_.SetStepOutput(step_id, std::move(*parsed));
  ExecuteStep(next_step_id);
}

void WorkflowRuntime::ExecuteAiDecide(const WorkflowStep& step) {
  std::string instruction = variable_store_.Resolve(step.ai_instruction);
  std::string outcomes_list;
  for (size_t i = 0; i < step.allowed_outcomes.size(); ++i) {
    if (i > 0) {
      outcomes_list += ", ";
    }
    outcomes_list += step.allowed_outcomes[i];
  }
  std::string prompt = base::StrCat(
      {instruction,
       "\n\nRespond with ONLY one of these exact words, no other text: ",
       outcomes_list});
  RunAiStep(prompt, base::BindOnce(&WorkflowRuntime::OnAiDecideResult,
                                   weak_ptr_factory_.GetWeakPtr(), step.id,
                                   step.ai_output_variable,
                                   step.allowed_outcomes, step.next));
}

void WorkflowRuntime::OnAiDecideResult(
    std::string step_id,
    std::string output_variable,
    std::vector<std::string> allowed_outcomes,
    std::string next_step_id,
    bool success,
    std::string text) {
  if (!success) {
    ExecutionResult result = result_;
    result.error_message =
        base::StrCat({"ai.decide step '", step_id, "' failed: ", text});
    Finish(std::move(result));
    return;
  }
  std::string trimmed;
  base::TrimWhitespaceASCII(text, base::TRIM_ALL, &trimmed);
  std::string chosen;
  for (const auto& outcome : allowed_outcomes) {
    if (base::EqualsCaseInsensitiveASCII(trimmed, outcome)) {
      chosen = outcome;
      break;
    }
  }
  if (chosen.empty()) {
    ExecutionResult result = result_;
    result.error_message = base::StrCat(
        {"ai.decide step '", step_id,
         "' returned an outcome not in the allowed list: '", trimmed, "'"});
    Finish(std::move(result));
    return;
  }
  variable_store_.SetVariable(output_variable, chosen);
  base::DictValue output_dict;
  output_dict.Set("outcome", chosen);
  variable_store_.SetStepOutput(step_id, base::Value(std::move(output_dict)));
  ExecuteStep(next_step_id);
}

void WorkflowRuntime::RunAiStep(const std::string& prompt,
                                AiStepCallback callback) {
  if (!model_service_) {
    std::move(callback).Run(false, "model service is unavailable");
    return;
  }
  std::string model_key = model_service_->GetDefaultModelKey();
  std::unique_ptr<EngineConsumer> engine = model_service_->GetEngineForModel(
      model_key, url_loader_factory_, /*credential_manager=*/nullptr);
  if (!engine) {
    std::move(callback).Run(false, "could not create a model engine");
    return;
  }

  auto state = std::make_shared<AiStepState>();
  state->engine = std::move(engine);
  state->callback = std::move(callback);

  EngineConsumer::ConversationHistory history;
  history.push_back(mojom::ConversationTurn::New(
      base::Uuid::GenerateRandomV4().AsLowercaseString(),
      mojom::CharacterType::HUMAN, mojom::ActionType::QUERY, prompt,
      std::nullopt /* prompt */, std::nullopt /* selected_text */,
      std::nullopt /* events */, base::Time::Now(), std::nullopt /* edits */,
      std::nullopt /* uploaded_files */, nullptr /* skill */,
      false /* from_brave_search_SERP */, std::nullopt /* model_key */,
      nullptr /* near_verification_status */));

  EngineConsumer* engine_ptr = state->engine.get();
  engine_ptr->GenerateAssistantResponse(
      {} /* page_contents */, history, /*is_temporary_chat=*/true,
      {} /* tools */, std::nullopt /* preferred_tool_name */,
      {} /* conversation_capabilities */,
      base::BindRepeating(&WorkflowRuntime::OnAiStepDataReceived,
                          weak_ptr_factory_.GetWeakPtr(), state),
      base::BindOnce(&WorkflowRuntime::OnAiStepCompleted,
                     weak_ptr_factory_.GetWeakPtr(), state));
}

void WorkflowRuntime::OnAiStepDataReceived(
    std::shared_ptr<AiStepState> state,
    EngineConsumer::GenerationResultData result) {
  if (!result.event || !result.event->is_completion_event()) {
    return;
  }
  if (state->engine->SupportsDeltaTextResponses()) {
    state->completion_text = base::StrCat(
        {state->completion_text, result.event->get_completion_event()->completion});
  } else {
    state->completion_text = result.event->get_completion_event()->completion;
  }
}

void WorkflowRuntime::OnAiStepCompleted(
    std::shared_ptr<AiStepState> state,
    EngineConsumer::GenerationResult result) {
  if (!result.has_value()) {
    std::move(state->callback).Run(false, "model failed to generate a response");
    return;
  }

  std::string final_text = state->completion_text;
  if (final_text.empty() && result->event && result->event->is_completion_event()) {
    final_text = result->event->get_completion_event()->completion;
  }
  if (final_text.empty()) {
    std::move(state->callback).Run(false, "model returned no text");
    return;
  }
  std::move(state->callback).Run(true, final_text);
}

void WorkflowRuntime::Finish(ExecutionResult result) {
  result.executed_step_ids = result_.executed_step_ids;
  std::move(callback_).Run(std::move(result));
  delete this;
}

}  // namespace ai_chat

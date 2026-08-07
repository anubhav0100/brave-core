// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_runtime.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {
constexpr int kMaxSteps = 200;  // Matches the design doc's example runtime_limits.
constexpr base::TimeDelta kNavigationTimeout = base::Seconds(20);
}  // namespace

WorkflowRuntime::ExecutionResult::ExecutionResult() = default;
WorkflowRuntime::ExecutionResult::ExecutionResult(const ExecutionResult&) =
    default;
WorkflowRuntime::ExecutionResult& WorkflowRuntime::ExecutionResult::operator=(
    const ExecutionResult&) = default;
WorkflowRuntime::ExecutionResult::~ExecutionResult() = default;

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
                            ResultCallback callback) {
  auto* runtime = new WorkflowRuntime(std::move(definition), std::move(inputs),
                                      web_contents, std::move(callback));
  runtime->Run();
}

WorkflowRuntime::WorkflowRuntime(WorkflowDefinition definition,
                                 base::DictValue inputs,
                                 content::WebContents* web_contents,
                                 ResultCallback callback)
    : definition_(std::move(definition)),
      web_contents_(web_contents),
      callback_(std::move(callback)) {
  for (auto [name, value] : inputs) {
    variable_store_.SetInput(name, std::move(value));
  }
  for (const auto& [name, value_expr] : definition_.initial_variables) {
    variable_store_.SetVariable(name, variable_store_.Resolve(value_expr));
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

  const WorkflowStep* step = FindStep(step_id);
  if (!step) {
    ExecutionResult result;
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

void WorkflowRuntime::Finish(ExecutionResult result) {
  result.executed_step_ids = result_.executed_step_ids;
  std::move(callback_).Run(std::move(result));
  delete this;
}

}  // namespace ai_chat

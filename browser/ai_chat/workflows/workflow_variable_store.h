// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_VARIABLE_STORE_H_
#define BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_VARIABLE_STORE_H_

#include <map>
#include <optional>
#include <string>

#include "base/values.h"

namespace ai_chat {

// Holds a running workflow execution's inputs, variables, per-step outputs,
// and current loop bindings, and resolves "${...}" expressions against
// them. Supported reference forms: "${input.NAME}", "${var.NAME}",
// "${step.STEP_ID.FIELD}" (a prior step's recorded output - FIELD may be
// omitted to get the whole output serialized), and "${loop.item}" /
// "${loop.index}" (the innermost currently-active for_each/while/until
// loop's current item/iteration count - see workflow_runtime.cc; nested
// loops each get their own for_each `item_variable`/`index_variable` bound
// as ordinary "${var.*}" names too, which is the more precise way to
// address an *outer* loop's value from inside an inner one, since
// "${loop.*}" always means the innermost).
class WorkflowVariableStore {
 public:
  WorkflowVariableStore();
  ~WorkflowVariableStore();

  WorkflowVariableStore(const WorkflowVariableStore&) = delete;
  WorkflowVariableStore& operator=(const WorkflowVariableStore&) = delete;

  void SetInput(const std::string& name, base::Value value);
  void SetVariable(const std::string& name, std::string value);
  std::optional<std::string> GetVariable(const std::string& name) const;

  // Records `value` as the given step's output, addressable afterwards as
  // "${step.STEP_ID}" (whole value, serialized if not a string) or
  // "${step.STEP_ID.FIELD}" (if `value` is a dict, that field's value).
  void SetStepOutput(const std::string& step_id, base::Value value);

  // Sets/clears the innermost active loop's current item/index, addressable
  // as "${loop.item}"/"${loop.index}" - called by WorkflowRuntime as it
  // enters, advances, and exits loops.
  void SetLoopItem(base::Value item);
  void SetLoopIndex(int index);
  void ClearLoopBindings();

  // Replaces every "${...}" occurrence in `text` with its current value
  // (missing references become empty strings; a non-string value is
  // JSON-serialized). Text with no "${...}" markers is returned unchanged.
  std::string Resolve(const std::string& text) const;

  // Evaluates a condition after resolving "${...}" references. Supports
  // (checked in this order): "==", "!=", ">=", "<=", ">", "<" (numeric
  // compare if both sides parse as numbers, else lexicographic),
  // "contains", "not_contains", "starts_with", "ends_with", "in", "not_in"
  // (right side is a JSON array or comma-separated list), "is_empty",
  // "is_not_empty" (also used for "exists"/"not_exists" - this simplified
  // resolve-then-parse model can't distinguish "reference didn't exist"
  // from "reference resolved to an empty string", so they're treated as
  // the same check). With no recognized operator: whether the resolved
  // text is non-empty and not "false" or "0".
  bool EvaluateCondition(const std::string& expression) const;

 private:
  std::map<std::string, base::Value> inputs_;
  std::map<std::string, std::string> variables_;
  std::map<std::string, base::Value> step_outputs_;
  std::optional<base::Value> loop_item_;
  std::optional<int> loop_index_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_VARIABLE_STORE_H_

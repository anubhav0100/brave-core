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

// Holds a running workflow execution's inputs and variables, and resolves
// "${...}" expressions against them - a deliberately minimal expression
// language for the V1 runtime (see the design doc's "Variable Model" and
// "Conditions and Branches" sections for the fuller language a later phase
// should implement, once call_flow/loops need to reference step outputs
// too).
//
// Supported reference forms: "${input.NAME}" and "${var.NAME}".
class WorkflowVariableStore {
 public:
  WorkflowVariableStore();
  ~WorkflowVariableStore();

  WorkflowVariableStore(const WorkflowVariableStore&) = delete;
  WorkflowVariableStore& operator=(const WorkflowVariableStore&) = delete;

  void SetInput(const std::string& name, base::Value value);
  void SetVariable(const std::string& name, std::string value);
  std::optional<std::string> GetVariable(const std::string& name) const;

  // Replaces every "${input.NAME}" and "${var.NAME}" occurrence in `text`
  // with its current value (missing references become empty strings; a
  // non-string input is JSON-serialized). Text with no "${...}" markers is
  // returned unchanged.
  std::string Resolve(const std::string& text) const;

  // Evaluates a condition after resolving "${...}" references:
  // "<left> == <right>" or "<left> != <right>" (both sides trimmed), or -
  // with no recognized operator - whether the resolved text is non-empty
  // and not "false" or "0".
  bool EvaluateCondition(const std::string& expression) const;

 private:
  std::map<std::string, base::Value> inputs_;
  std::map<std::string, std::string> variables_;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_VARIABLE_STORE_H_

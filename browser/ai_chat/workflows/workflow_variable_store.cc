// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_variable_store.h"

#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/string_util.h"

namespace ai_chat {

namespace {

std::string ResolveSingleReference(
    const std::string& reference,
    const std::map<std::string, base::Value>& inputs,
    const std::map<std::string, std::string>& variables) {
  size_t dot = reference.find('.');
  if (dot == std::string::npos) {
    return "";
  }
  std::string scope = reference.substr(0, dot);
  std::string name = reference.substr(dot + 1);
  base::TrimWhitespaceASCII(scope, base::TRIM_ALL, &scope);
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);

  if (scope == "var") {
    auto it = variables.find(name);
    return it != variables.end() ? it->second : "";
  }
  if (scope == "input") {
    auto it = inputs.find(name);
    if (it == inputs.end()) {
      return "";
    }
    if (it->second.is_string()) {
      return it->second.GetString();
    }
    std::string serialized;
    base::JSONWriter::Write(it->second, &serialized);
    return serialized;
  }
  return "";
}

}  // namespace

WorkflowVariableStore::WorkflowVariableStore() = default;
WorkflowVariableStore::~WorkflowVariableStore() = default;

void WorkflowVariableStore::SetInput(const std::string& name,
                                     base::Value value) {
  inputs_[name] = std::move(value);
}

void WorkflowVariableStore::SetVariable(const std::string& name,
                                        std::string value) {
  variables_[name] = std::move(value);
}

std::optional<std::string> WorkflowVariableStore::GetVariable(
    const std::string& name) const {
  auto it = variables_.find(name);
  if (it == variables_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string WorkflowVariableStore::Resolve(const std::string& text) const {
  std::string result;
  result.reserve(text.size());
  size_t pos = 0;
  while (pos < text.size()) {
    size_t start = text.find("${", pos);
    if (start == std::string::npos) {
      result.append(text, pos, std::string::npos);
      break;
    }
    result.append(text, pos, start - pos);
    size_t end = text.find('}', start + 2);
    if (end == std::string::npos) {
      result.append(text, start, std::string::npos);
      break;
    }
    std::string reference = text.substr(start + 2, end - start - 2);
    result += ResolveSingleReference(reference, inputs_, variables_);
    pos = end + 1;
  }
  return result;
}

bool WorkflowVariableStore::EvaluateCondition(
    const std::string& expression) const {
  std::string resolved = Resolve(expression);

  size_t eq = resolved.find("==");
  size_t neq = resolved.find("!=");
  if (eq != std::string::npos && (neq == std::string::npos || eq < neq)) {
    std::string left = resolved.substr(0, eq);
    std::string right = resolved.substr(eq + 2);
    base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
    base::TrimWhitespaceASCII(right, base::TRIM_ALL, &right);
    return left == right;
  }
  if (neq != std::string::npos) {
    std::string left = resolved.substr(0, neq);
    std::string right = resolved.substr(neq + 2);
    base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
    base::TrimWhitespaceASCII(right, base::TRIM_ALL, &right);
    return left != right;
  }

  std::string trimmed;
  base::TrimWhitespaceASCII(resolved, base::TRIM_ALL, &trimmed);
  return !trimmed.empty() && trimmed != "false" && trimmed != "0";
}

}  // namespace ai_chat

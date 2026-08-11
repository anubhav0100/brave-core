// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_variable_store.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace ai_chat {

namespace {

std::string ValueToText(const base::Value& value) {
  if (value.is_string()) {
    return value.GetString();
  }
  std::string serialized;
  base::JSONWriter::Write(value, &serialized);
  return serialized;
}

// Parses "a,b,c" or a JSON array string like `["a","b"]` into a plain list
// of trimmed strings, for the "in"/"not_in" operators.
std::vector<std::string> ParseMembershipList(const std::string& text) {
  std::vector<std::string> items;
  if (auto parsed = base::JSONReader::Read(
          text, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
      parsed && parsed->is_list()) {
    for (const auto& entry : parsed->GetList()) {
      items.push_back(ValueToText(entry));
    }
    return items;
  }
  for (const auto& piece : base::SplitString(
           text, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    items.push_back(piece);
  }
  return items;
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

void WorkflowVariableStore::SetStepOutput(const std::string& step_id,
                                          base::Value value) {
  step_outputs_[step_id] = std::move(value);
}

void WorkflowVariableStore::SetLoopItem(base::Value item) {
  loop_item_ = std::move(item);
}

void WorkflowVariableStore::SetLoopIndex(int index) {
  loop_index_ = index;
}

void WorkflowVariableStore::ClearLoopBindings() {
  loop_item_.reset();
  loop_index_.reset();
}

namespace {

// Resolves one "scope.name" (or "scope.name.field") reference. A member
// function would need the same five members passed in anyway - kept as a
// free function taking exactly what it needs, called from
// WorkflowVariableStore::Resolve().
std::string ResolveSingleReference(
    const std::string& reference,
    const std::map<std::string, base::Value>& inputs,
    const std::map<std::string, std::string>& variables,
    const std::map<std::string, base::Value>& step_outputs,
    const std::optional<base::Value>& loop_item,
    const std::optional<int>& loop_index) {
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
    return it != inputs.end() ? ValueToText(it->second) : "";
  }
  if (scope == "step") {
    size_t field_dot = name.find('.');
    std::string step_id =
        field_dot == std::string::npos ? name : name.substr(0, field_dot);
    std::string field =
        field_dot == std::string::npos ? "" : name.substr(field_dot + 1);
    auto it = step_outputs.find(step_id);
    if (it == step_outputs.end()) {
      return "";
    }
    if (field.empty()) {
      return ValueToText(it->second);
    }
    if (const base::DictValue* dict = it->second.GetIfDict()) {
      if (const base::Value* field_value = dict->Find(field)) {
        return ValueToText(*field_value);
      }
    }
    return "";
  }
  if (scope == "loop") {
    if (name == "item") {
      return loop_item ? ValueToText(*loop_item) : "";
    }
    if (name == "index") {
      return loop_index ? base::NumberToString(*loop_index) : "";
    }
    return "";
  }
  return "";
}

}  // namespace

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
    result += ResolveSingleReference(reference, inputs_, variables_,
                                     step_outputs_, loop_item_, loop_index_);
    pos = end + 1;
  }
  return result;
}

namespace {

enum class OperatorKind { kBinary, kUnarySuffix };

struct OperatorSpec {
  const char* token;
  OperatorKind kind;
};

// Order doesn't affect correctness (see EvaluateCondition's comment on why
// padded whole-token search avoids collisions between e.g. ">" and ">=",
// or "contains" and "not_contains") - listed roughly doc-order.
constexpr std::array<OperatorSpec, 14> kOperators = {{
    {"==", OperatorKind::kBinary},
    {"!=", OperatorKind::kBinary},
    {">=", OperatorKind::kBinary},
    {"<=", OperatorKind::kBinary},
    {">", OperatorKind::kBinary},
    {"<", OperatorKind::kBinary},
    {"not_contains", OperatorKind::kBinary},
    {"contains", OperatorKind::kBinary},
    {"starts_with", OperatorKind::kBinary},
    {"ends_with", OperatorKind::kBinary},
    {"not_in", OperatorKind::kBinary},
    {"in", OperatorKind::kBinary},
    {"is_not_empty", OperatorKind::kUnarySuffix},
    {"is_empty", OperatorKind::kUnarySuffix},
    // "exists"/"not_exists" are handled as aliases below, not searched here.
}};

}  // namespace

bool WorkflowVariableStore::EvaluateCondition(
    const std::string& expression) const {
  std::string resolved = Resolve(expression);

  // Unary suffix operators first (no ambiguity with binary search below
  // since they're checked via EndsWith, not substring position).
  for (const auto& op : kOperators) {
    if (op.kind != OperatorKind::kUnarySuffix) {
      continue;
    }
    std::string suffix = std::string(" ") + op.token;
    if (base::EndsWith(resolved, suffix)) {
      std::string left = resolved.substr(0, resolved.size() - suffix.size());
      base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
      bool is_empty = left.empty();
      return std::string(op.token) == "is_empty" ? is_empty : !is_empty;
    }
  }
  if (base::EndsWith(resolved, " exists") &&
      !base::EndsWith(resolved, " not_exists")) {
    std::string left = resolved.substr(0, resolved.size() - 7);
    base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
    return !left.empty();
  }
  if (base::EndsWith(resolved, " not_exists")) {
    std::string left = resolved.substr(0, resolved.size() - 11);
    base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
    return left.empty();
  }

  // Binary operators: find the leftmost match among all candidates.
  const OperatorSpec* best = nullptr;
  size_t best_pos = std::string::npos;
  for (const auto& op : kOperators) {
    if (op.kind != OperatorKind::kBinary) {
      continue;
    }
    std::string padded = std::string(" ") + op.token + " ";
    size_t pos = resolved.find(padded);
    if (pos != std::string::npos && pos < best_pos) {
      best_pos = pos;
      best = &op;
    }
  }

  if (best) {
    std::string padded = std::string(" ") + best->token + " ";
    std::string left = resolved.substr(0, best_pos);
    std::string right = resolved.substr(best_pos + padded.size());
    base::TrimWhitespaceASCII(left, base::TRIM_ALL, &left);
    base::TrimWhitespaceASCII(right, base::TRIM_ALL, &right);
    std::string token = best->token;

    if (token == "==") {
      return left == right;
    }
    if (token == "!=") {
      return left != right;
    }
    if (token == "contains") {
      return left.find(right) != std::string::npos;
    }
    if (token == "not_contains") {
      return left.find(right) == std::string::npos;
    }
    if (token == "starts_with") {
      return base::StartsWith(left, right);
    }
    if (token == "ends_with") {
      return base::EndsWith(left, right);
    }
    if (token == "in") {
      auto items = ParseMembershipList(right);
      return std::ranges::find(items, left) != items.end();
    }
    if (token == "not_in") {
      auto items = ParseMembershipList(right);
      return std::ranges::find(items, left) == items.end();
    }
    // Ordering operators: numeric compare if both sides parse, else
    // lexicographic.
    double left_num, right_num;
    bool both_numeric = base::StringToDouble(left, &left_num) &&
                        base::StringToDouble(right, &right_num);
    if (token == ">") {
      return both_numeric ? left_num > right_num : left > right;
    }
    if (token == ">=") {
      return both_numeric ? left_num >= right_num : left >= right;
    }
    if (token == "<") {
      return both_numeric ? left_num < right_num : left < right;
    }
    if (token == "<=") {
      return both_numeric ? left_num <= right_num : left <= right;
    }
  }

  std::string trimmed;
  base::TrimWhitespaceASCII(resolved, base::TRIM_ALL, &trimmed);
  return !trimmed.empty() && trimmed != "false" && trimmed != "0";
}

}  // namespace ai_chat

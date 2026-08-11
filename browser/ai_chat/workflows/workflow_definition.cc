// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_definition.h"

#include <set>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"

namespace ai_chat {

namespace {

constexpr char kTypeStart[] = "start";
constexpr char kTypeComplete[] = "complete";
constexpr char kTypeFail[] = "fail";
constexpr char kTypeSetVariable[] = "set_variable";
constexpr char kTypeCondition[] = "condition";
constexpr char kTypeBrowserNavigate[] = "browser.navigate";
constexpr char kTypeBrowserClick[] = "browser.click";
constexpr char kTypeBrowserType[] = "browser.type";
constexpr char kTypeBrowserWait[] = "browser.wait";
constexpr char kTypeCallFlow[] = "call_flow";
constexpr char kTypeForEach[] = "for_each";
constexpr char kTypeWhile[] = "while";
constexpr char kTypeUntil[] = "until";
constexpr char kTypeBreak[] = "break";
constexpr char kTypeContinue[] = "continue";
constexpr char kTypeAiExtract[] = "ai.extract";
constexpr char kTypeAiDecide[] = "ai.decide";

// Terminal step types don't need a `next` - execution stops there.
bool IsTerminalType(WorkflowStepType type) {
  return type == WorkflowStepType::kComplete ||
         type == WorkflowStepType::kFail;
}

// break/continue act on the innermost active loop's own stored
// continuation, not on a `next` field of their own - see workflow_runtime.cc.
bool StepUsesLoopContextInsteadOfNext(WorkflowStepType type) {
  return type == WorkflowStepType::kBreak ||
         type == WorkflowStepType::kContinue;
}

bool IsLoopType(WorkflowStepType type) {
  return type == WorkflowStepType::kForEach ||
         type == WorkflowStepType::kWhile || type == WorkflowStepType::kUntil;
}

}  // namespace

std::optional<WorkflowStepType> WorkflowStepTypeFromString(
    const std::string& type) {
  if (type == kTypeStart) {
    return WorkflowStepType::kStart;
  }
  if (type == kTypeComplete) {
    return WorkflowStepType::kComplete;
  }
  if (type == kTypeFail) {
    return WorkflowStepType::kFail;
  }
  if (type == kTypeSetVariable) {
    return WorkflowStepType::kSetVariable;
  }
  if (type == kTypeCondition) {
    return WorkflowStepType::kCondition;
  }
  if (type == kTypeBrowserNavigate) {
    return WorkflowStepType::kBrowserNavigate;
  }
  if (type == kTypeBrowserClick) {
    return WorkflowStepType::kBrowserClick;
  }
  if (type == kTypeBrowserType) {
    return WorkflowStepType::kBrowserType;
  }
  if (type == kTypeBrowserWait) {
    return WorkflowStepType::kBrowserWait;
  }
  if (type == kTypeCallFlow) {
    return WorkflowStepType::kCallFlow;
  }
  if (type == kTypeForEach) {
    return WorkflowStepType::kForEach;
  }
  if (type == kTypeWhile) {
    return WorkflowStepType::kWhile;
  }
  if (type == kTypeUntil) {
    return WorkflowStepType::kUntil;
  }
  if (type == kTypeBreak) {
    return WorkflowStepType::kBreak;
  }
  if (type == kTypeContinue) {
    return WorkflowStepType::kContinue;
  }
  if (type == kTypeAiExtract) {
    return WorkflowStepType::kAiExtract;
  }
  if (type == kTypeAiDecide) {
    return WorkflowStepType::kAiDecide;
  }
  return std::nullopt;
}

std::string WorkflowStepTypeToString(WorkflowStepType type) {
  switch (type) {
    case WorkflowStepType::kStart:
      return kTypeStart;
    case WorkflowStepType::kComplete:
      return kTypeComplete;
    case WorkflowStepType::kFail:
      return kTypeFail;
    case WorkflowStepType::kSetVariable:
      return kTypeSetVariable;
    case WorkflowStepType::kCondition:
      return kTypeCondition;
    case WorkflowStepType::kBrowserNavigate:
      return kTypeBrowserNavigate;
    case WorkflowStepType::kBrowserClick:
      return kTypeBrowserClick;
    case WorkflowStepType::kBrowserType:
      return kTypeBrowserType;
    case WorkflowStepType::kBrowserWait:
      return kTypeBrowserWait;
    case WorkflowStepType::kCallFlow:
      return kTypeCallFlow;
    case WorkflowStepType::kForEach:
      return kTypeForEach;
    case WorkflowStepType::kWhile:
      return kTypeWhile;
    case WorkflowStepType::kUntil:
      return kTypeUntil;
    case WorkflowStepType::kBreak:
      return kTypeBreak;
    case WorkflowStepType::kContinue:
      return kTypeContinue;
    case WorkflowStepType::kAiExtract:
      return kTypeAiExtract;
    case WorkflowStepType::kAiDecide:
      return kTypeAiDecide;
  }
  return "";
}

std::string WorkflowStatusToString(WorkflowStatus status) {
  switch (status) {
    case WorkflowStatus::kDraft:
      return "draft";
    case WorkflowStatus::kPublished:
      return "published";
    case WorkflowStatus::kDeprecated:
      return "deprecated";
  }
  return "draft";
}

std::optional<WorkflowStatus> WorkflowStatusFromString(
    const std::string& status) {
  if (status == "draft") {
    return WorkflowStatus::kDraft;
  }
  if (status == "published") {
    return WorkflowStatus::kPublished;
  }
  if (status == "deprecated") {
    return WorkflowStatus::kDeprecated;
  }
  return std::nullopt;
}

WorkflowStep::WorkflowStep() = default;
WorkflowStep::WorkflowStep(const WorkflowStep&) = default;
WorkflowStep& WorkflowStep::operator=(const WorkflowStep&) = default;
WorkflowStep::WorkflowStep(WorkflowStep&&) = default;
WorkflowStep& WorkflowStep::operator=(WorkflowStep&&) = default;
WorkflowStep::~WorkflowStep() = default;

WorkflowDefinition::WorkflowDefinition() = default;
WorkflowDefinition::WorkflowDefinition(const WorkflowDefinition&) = default;
WorkflowDefinition& WorkflowDefinition::operator=(const WorkflowDefinition&) =
    default;
WorkflowDefinition::WorkflowDefinition(WorkflowDefinition&&) = default;
WorkflowDefinition& WorkflowDefinition::operator=(WorkflowDefinition&&) =
    default;
WorkflowDefinition::~WorkflowDefinition() = default;

WorkflowParseResult::WorkflowParseResult() = default;
WorkflowParseResult::WorkflowParseResult(const WorkflowParseResult&) =
    default;
WorkflowParseResult& WorkflowParseResult::operator=(
    const WorkflowParseResult&) = default;
WorkflowParseResult::WorkflowParseResult(WorkflowParseResult&&) = default;
WorkflowParseResult& WorkflowParseResult::operator=(WorkflowParseResult&&) =
    default;
WorkflowParseResult::~WorkflowParseResult() = default;

base::DictValue WorkflowDefinition::ToValue() const {
  base::DictValue root;
  root.Set("schema_version", "1.0");
  root.Set("id", id);
  root.Set("name", name);
  root.Set("description", description);
  root.Set("version", version);
  root.Set("status", WorkflowStatusToString(status));

  base::DictValue inputs_value;
  for (const auto& [input_name, spec] : inputs) {
    base::DictValue spec_value;
    spec_value.Set("type", spec.type);
    spec_value.Set("required", spec.required);
    inputs_value.Set(input_name, std::move(spec_value));
  }
  root.Set("inputs", std::move(inputs_value));

  base::DictValue outputs_value;
  for (const auto& [output_name, spec] : outputs) {
    base::DictValue spec_value;
    spec_value.Set("type", spec.type);
    outputs_value.Set(output_name, std::move(spec_value));
  }
  root.Set("outputs", std::move(outputs_value));

  base::DictValue variables_value;
  for (const auto& [variable_name, variable_value] : initial_variables) {
    variables_value.Set(variable_name, variable_value);
  }
  root.Set("variables", std::move(variables_value));

  base::ListValue steps_value;
  for (const auto& step : steps) {
    base::DictValue step_value;
    step_value.Set("id", step.id);
    step_value.Set("type", WorkflowStepTypeToString(step.type));
    if (!step.next.empty()) {
      step_value.Set("next", step.next);
    }
    switch (step.type) {
      case WorkflowStepType::kSetVariable:
        step_value.Set("name", step.variable_name);
        step_value.Set("value", step.variable_value);
        break;
      case WorkflowStepType::kCondition:
        step_value.Set("expression", step.condition_expression);
        step_value.Set("on_true", step.on_true);
        step_value.Set("on_false", step.on_false);
        break;
      case WorkflowStepType::kBrowserNavigate:
        step_value.Set("url", step.url);
        break;
      case WorkflowStepType::kBrowserClick:
        step_value.Set("selector", step.selector);
        break;
      case WorkflowStepType::kBrowserType:
        step_value.Set("selector", step.selector);
        step_value.Set("text", step.text);
        break;
      case WorkflowStepType::kBrowserWait:
        step_value.Set("seconds", step.wait_seconds);
        break;
      case WorkflowStepType::kFail:
        step_value.Set("reason", step.fail_reason);
        break;
      case WorkflowStepType::kComplete: {
        base::DictValue outputs_dict;
        for (const auto& [output_name, expr] : step.outputs) {
          outputs_dict.Set(output_name, expr);
        }
        step_value.Set("outputs", std::move(outputs_dict));
        break;
      }
      case WorkflowStepType::kCallFlow: {
        step_value.Set("flow_id", step.flow_id);
        step_value.Set("on_child_failure", step.on_child_failure);
        base::DictValue inputs_dict;
        for (const auto& [input_name, expr] : step.call_inputs) {
          inputs_dict.Set(input_name, expr);
        }
        step_value.Set("inputs", std::move(inputs_dict));
        base::DictValue outputs_dict;
        for (const auto& [child_output, parent_var] : step.call_outputs) {
          outputs_dict.Set(child_output, parent_var);
        }
        step_value.Set("outputs", std::move(outputs_dict));
        break;
      }
      case WorkflowStepType::kForEach:
        step_value.Set("items", step.items_expression);
        step_value.Set("item_variable", step.item_variable);
        if (!step.index_variable.empty()) {
          step_value.Set("index_variable", step.index_variable);
        }
        step_value.Set("body_start", step.body_start);
        step_value.Set("max_iterations", step.max_iterations);
        break;
      case WorkflowStepType::kWhile:
      case WorkflowStepType::kUntil:
        step_value.Set("condition", step.loop_condition);
        step_value.Set("body_start", step.body_start);
        step_value.Set("max_iterations", step.max_iterations);
        break;
      case WorkflowStepType::kBreak:
      case WorkflowStepType::kContinue:
        break;
      case WorkflowStepType::kAiExtract:
        step_value.Set("instruction", step.ai_instruction);
        step_value.Set("schema", step.ai_schema_json);
        step_value.Set("output_variable", step.ai_output_variable);
        break;
      case WorkflowStepType::kAiDecide: {
        step_value.Set("instruction", step.ai_instruction);
        step_value.Set("output_variable", step.ai_output_variable);
        base::ListValue outcomes;
        for (const auto& outcome : step.allowed_outcomes) {
          outcomes.Append(outcome);
        }
        step_value.Set("allowed_outcomes", std::move(outcomes));
        break;
      }
      case WorkflowStepType::kStart:
        break;
    }
    steps_value.Append(std::move(step_value));
  }
  root.Set("steps", std::move(steps_value));

  return root;
}

namespace {

std::string ExpectString(const base::DictValue& dict,
                         const std::string& key,
                         const std::string& default_value = "") {
  const std::string* value = dict.FindString(key);
  return value ? *value : default_value;
}

}  // namespace

WorkflowParseResult ParseWorkflowDefinition(const base::DictValue& value) {
  WorkflowParseResult result;
  WorkflowDefinition definition;

  definition.id = ExpectString(value, "id");
  definition.name = ExpectString(value, "name");
  definition.description = ExpectString(value, "description");
  definition.version = ExpectString(value, "version", "1.0.0");

  std::string status_str = ExpectString(value, "status", "draft");
  std::optional<WorkflowStatus> status = WorkflowStatusFromString(status_str);
  if (!status) {
    result.errors.push_back(
        {"", "Unknown workflow status: '" + status_str + "'"});
    status = WorkflowStatus::kDraft;
  }
  definition.status = *status;

  if (definition.id.empty()) {
    result.errors.push_back({"", "Workflow is missing required field 'id'"});
  }
  if (definition.name.empty()) {
    result.errors.push_back(
        {"", "Workflow is missing required field 'name'"});
  }

  if (const base::DictValue* inputs = value.FindDict("inputs")) {
    for (const auto [name, input_value] : *inputs) {
      if (const base::DictValue* spec_dict = input_value.GetIfDict()) {
        WorkflowInputSpec spec;
        spec.type = ExpectString(*spec_dict, "type", "string");
        spec.required = spec_dict->FindBool("required").value_or(false);
        definition.inputs[name] = std::move(spec);
      }
    }
  }

  if (const base::DictValue* outputs = value.FindDict("outputs")) {
    for (const auto [name, output_value] : *outputs) {
      if (const base::DictValue* spec_dict = output_value.GetIfDict()) {
        WorkflowOutputSpec spec;
        spec.type = ExpectString(*spec_dict, "type", "string");
        definition.outputs[name] = std::move(spec);
      }
    }
  }

  if (const base::DictValue* variables = value.FindDict("variables")) {
    for (const auto [name, var_value] : *variables) {
      if (const std::string* str = var_value.GetIfString()) {
        definition.initial_variables[name] = *str;
      } else {
        std::string serialized;
        base::JSONWriter::Write(var_value, &serialized);
        definition.initial_variables[name] = serialized;
      }
    }
  }

  std::set<std::string> seen_step_ids;
  const base::ListValue* steps = value.FindList("steps");
  if (!steps || steps->empty()) {
    result.errors.push_back(
        {"", "Workflow must have at least one step in 'steps'"});
  } else {
    for (const auto& step_value : *steps) {
      const base::DictValue* step_dict = step_value.GetIfDict();
      if (!step_dict) {
        result.errors.push_back({"", "A step entry is not an object"});
        continue;
      }
      std::string step_id = ExpectString(*step_dict, "id");
      if (step_id.empty()) {
        result.errors.push_back({"", "A step is missing required field 'id'"});
        continue;
      }
      if (!seen_step_ids.insert(step_id).second) {
        result.errors.push_back(
            {step_id, "Duplicate step id: '" + step_id + "'"});
        continue;
      }

      std::string type_str = ExpectString(*step_dict, "type");
      std::optional<WorkflowStepType> type =
          WorkflowStepTypeFromString(type_str);
      if (!type) {
        result.errors.push_back(
            {step_id,
             "Unknown or not-yet-supported step type: '" + type_str +
                 "' (this runtime supports start, complete, fail, "
                 "set_variable, condition, browser.navigate, browser.click, "
                 "browser.type, browser.wait, call_flow, for_each, while, "
                 "until, break, continue, ai.extract, ai.decide)"});
        continue;
      }

      WorkflowStep step;
      step.id = step_id;
      step.type = *type;
      step.next = ExpectString(*step_dict, "next");

      switch (*type) {
        case WorkflowStepType::kSetVariable:
          step.variable_name = ExpectString(*step_dict, "name");
          step.variable_value = ExpectString(*step_dict, "value");
          if (step.variable_name.empty()) {
            result.errors.push_back(
                {step_id, "set_variable step is missing 'name'"});
          }
          break;
        case WorkflowStepType::kCondition:
          step.condition_expression = ExpectString(*step_dict, "expression");
          step.on_true = ExpectString(*step_dict, "on_true");
          step.on_false = ExpectString(*step_dict, "on_false");
          if (step.condition_expression.empty()) {
            result.errors.push_back(
                {step_id, "condition step is missing 'expression'"});
          }
          break;
        case WorkflowStepType::kBrowserNavigate:
          step.url = ExpectString(*step_dict, "url");
          if (step.url.empty()) {
            result.errors.push_back(
                {step_id, "browser.navigate step is missing 'url'"});
          }
          break;
        case WorkflowStepType::kBrowserClick:
          step.selector = ExpectString(*step_dict, "selector");
          if (step.selector.empty()) {
            result.errors.push_back(
                {step_id, "browser.click step is missing 'selector'"});
          }
          break;
        case WorkflowStepType::kBrowserType:
          step.selector = ExpectString(*step_dict, "selector");
          step.text = ExpectString(*step_dict, "text");
          if (step.selector.empty()) {
            result.errors.push_back(
                {step_id, "browser.type step is missing 'selector'"});
          }
          break;
        case WorkflowStepType::kBrowserWait:
          step.wait_seconds = step_dict->FindInt("seconds").value_or(0);
          if (step.wait_seconds <= 0) {
            result.errors.push_back(
                {step_id, "browser.wait step needs a positive 'seconds'"});
          }
          break;
        case WorkflowStepType::kFail:
          step.fail_reason = ExpectString(*step_dict, "reason");
          break;
        case WorkflowStepType::kComplete:
          if (const base::DictValue* outputs_dict =
                  step_dict->FindDict("outputs")) {
            for (const auto [name, expr_value] : *outputs_dict) {
              if (const std::string* expr = expr_value.GetIfString()) {
                step.outputs[name] = *expr;
              }
            }
          }
          break;
        case WorkflowStepType::kCallFlow:
          step.flow_id = ExpectString(*step_dict, "flow_id");
          step.on_child_failure =
              ExpectString(*step_dict, "on_child_failure", "fail_parent");
          if (step.flow_id.empty()) {
            result.errors.push_back(
                {step_id, "call_flow step is missing 'flow_id'"});
          }
          if (step.on_child_failure != "fail_parent" &&
              step.on_child_failure != "continue") {
            result.errors.push_back(
                {step_id, "call_flow step's 'on_child_failure' must be "
                          "'fail_parent' or 'continue', got '" +
                              step.on_child_failure + "'"});
          }
          if (const base::DictValue* inputs_dict =
                  step_dict->FindDict("inputs")) {
            for (const auto [name, expr_value] : *inputs_dict) {
              if (const std::string* expr = expr_value.GetIfString()) {
                step.call_inputs[name] = *expr;
              }
            }
          }
          if (const base::DictValue* outputs_dict =
                  step_dict->FindDict("outputs")) {
            for (const auto [child_output, var_value] : *outputs_dict) {
              if (const std::string* parent_var = var_value.GetIfString()) {
                step.call_outputs[child_output] = *parent_var;
              }
            }
          }
          break;
        case WorkflowStepType::kForEach:
          step.items_expression = ExpectString(*step_dict, "items");
          step.item_variable = ExpectString(*step_dict, "item_variable");
          step.index_variable = ExpectString(*step_dict, "index_variable");
          step.body_start = ExpectString(*step_dict, "body_start");
          step.max_iterations =
              step_dict->FindInt("max_iterations").value_or(1000);
          if (step.items_expression.empty()) {
            result.errors.push_back(
                {step_id, "for_each step is missing 'items'"});
          }
          if (step.item_variable.empty()) {
            result.errors.push_back(
                {step_id, "for_each step is missing 'item_variable'"});
          }
          if (step.body_start.empty()) {
            result.errors.push_back(
                {step_id, "for_each step is missing 'body_start'"});
          }
          break;
        case WorkflowStepType::kWhile:
        case WorkflowStepType::kUntil:
          step.loop_condition = ExpectString(*step_dict, "condition");
          step.body_start = ExpectString(*step_dict, "body_start");
          step.max_iterations =
              step_dict->FindInt("max_iterations").value_or(1000);
          if (step.loop_condition.empty()) {
            result.errors.push_back(
                {step_id, WorkflowStepTypeToString(*type) +
                              " step is missing 'condition'"});
          }
          if (step.body_start.empty()) {
            result.errors.push_back(
                {step_id, WorkflowStepTypeToString(*type) +
                              " step is missing 'body_start'"});
          }
          break;
        case WorkflowStepType::kBreak:
        case WorkflowStepType::kContinue:
          break;
        case WorkflowStepType::kAiExtract:
          step.ai_instruction = ExpectString(*step_dict, "instruction");
          step.ai_schema_json = ExpectString(*step_dict, "schema");
          step.ai_output_variable =
              ExpectString(*step_dict, "output_variable");
          if (step.ai_instruction.empty()) {
            result.errors.push_back(
                {step_id, "ai.extract step is missing 'instruction'"});
          }
          if (step.ai_schema_json.empty()) {
            result.errors.push_back(
                {step_id, "ai.extract step is missing 'schema'"});
          }
          if (step.ai_output_variable.empty()) {
            result.errors.push_back(
                {step_id, "ai.extract step is missing 'output_variable'"});
          }
          break;
        case WorkflowStepType::kAiDecide:
          step.ai_instruction = ExpectString(*step_dict, "instruction");
          step.ai_output_variable =
              ExpectString(*step_dict, "output_variable");
          if (const base::ListValue* outcomes =
                  step_dict->FindList("allowed_outcomes")) {
            for (const auto& outcome_value : *outcomes) {
              if (const std::string* outcome = outcome_value.GetIfString()) {
                step.allowed_outcomes.push_back(*outcome);
              }
            }
          }
          if (step.ai_instruction.empty()) {
            result.errors.push_back(
                {step_id, "ai.decide step is missing 'instruction'"});
          }
          if (step.allowed_outcomes.size() < 2) {
            result.errors.push_back(
                {step_id, "ai.decide step needs at least 2 "
                          "'allowed_outcomes'"});
          }
          if (step.ai_output_variable.empty()) {
            result.errors.push_back(
                {step_id, "ai.decide step is missing 'output_variable'"});
          }
          break;
        case WorkflowStepType::kStart:
          break;
      }

      if (!IsTerminalType(step.type) && step.next.empty() &&
          step.type != WorkflowStepType::kCondition &&
          !StepUsesLoopContextInsteadOfNext(step.type)) {
        result.errors.push_back(
            {step_id, "Step has no 'next' and isn't a terminal (complete/"
                      "fail), condition, break, or continue step - "
                      "execution would have nowhere to go"});
      }

      definition.steps.push_back(std::move(step));
    }
  }

  // Reference validation: exactly one start step, and every next/on_true/
  // on_false points at a real step id.
  int start_count = 0;
  for (const auto& step : definition.steps) {
    if (step.type == WorkflowStepType::kStart) {
      ++start_count;
    }
  }
  if (start_count == 0 && !definition.steps.empty()) {
    result.errors.push_back(
        {"", "Workflow must have exactly one step of type 'start'"});
  } else if (start_count > 1) {
    result.errors.push_back(
        {"", "Workflow must have exactly one step of type 'start', found " +
                 base::NumberToString(start_count)});
  }

  auto check_reference = [&](const std::string& from_step_id,
                             const std::string& field_name,
                             const std::string& target_id) {
    if (target_id.empty() || seen_step_ids.contains(target_id)) {
      return;
    }
    result.errors.push_back(
        {from_step_id, "'" + field_name + "' references unknown step id '" +
                           target_id + "'"});
  };
  for (const auto& step : definition.steps) {
    check_reference(step.id, "next", step.next);
    if (step.type == WorkflowStepType::kCondition) {
      check_reference(step.id, "on_true", step.on_true);
      check_reference(step.id, "on_false", step.on_false);
    }
    if (IsLoopType(step.type)) {
      check_reference(step.id, "body_start", step.body_start);
    }
  }

  definition.status = *status;
  result.definition = std::move(definition);
  return result;
}

}  // namespace ai_chat

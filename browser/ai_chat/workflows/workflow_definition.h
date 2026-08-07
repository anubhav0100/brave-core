// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_DEFINITION_H_
#define BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_DEFINITION_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/values.h"

// A workflow definition and its JSON (de)serialization - the "execution
// contract" for the workflow orchestration engine described in
// brave-ai-assistant-workflow-orchestration-engine.md. This implements that
// document's own recommended starting point (section 78, Phase 1: "Do not
// build the visual editor first. Establish the execution contract first."):
// a saved, versioned, machine-readable definition that a runtime (see
// workflow_runtime.h) can execute deterministically.
//
// V1 scope covers exactly the node types the document's Phase 3 ("Basic
// Runtime") calls for - start, set_variable, condition, browser.navigate,
// browser.click, browser.type, browser.wait, complete, fail - for one flat
// workflow with no nested flows, loops, AI nodes, tools, webhooks, or
// approvals yet (those are later phases in the document's own plan; see
// workflow_runtime.h's file comment for what's intentionally not
// implemented).
namespace ai_chat {

enum class WorkflowStepType {
  kStart,
  kComplete,
  kFail,
  kSetVariable,
  kCondition,
  kBrowserNavigate,
  kBrowserClick,
  kBrowserType,
  kBrowserWait,
};

// Returns nullopt for a type string this V1 runtime doesn't implement yet
// (e.g. "call_flow", "for_each", "ai.action", "tool.call", "webhook.call",
// "approval") - callers should surface that as a clear validation error
// rather than silently skipping the step.
std::optional<WorkflowStepType> WorkflowStepTypeFromString(
    const std::string& type);
std::string WorkflowStepTypeToString(WorkflowStepType type);

// One step in a workflow. Only the fields relevant to `type` are set; see
// workflow_definition.cc's parser for exactly which fields each type reads.
// String fields that support "${...}" substitution (see
// workflow_variable_store.h) are noted below.
struct WorkflowStep {
  WorkflowStep();
  WorkflowStep(const WorkflowStep&);
  WorkflowStep& operator=(const WorkflowStep&);
  WorkflowStep(WorkflowStep&&);
  WorkflowStep& operator=(WorkflowStep&&);
  ~WorkflowStep();

  std::string id;
  WorkflowStepType type = WorkflowStepType::kStart;

  // start, set_variable, browser.navigate, browser.click, browser.type,
  // browser.wait: id of the step to run next. Empty means the workflow ends
  // here (only valid for complete/fail, or a dead-end caught by the
  // validator).
  std::string next;

  // set_variable
  std::string variable_name;
  std::string variable_value;  // Supports "${...}".

  // condition
  std::string condition_expression;  // e.g. "${var.x} == some value".
  std::string on_true;
  std::string on_false;

  // browser.navigate
  std::string url;  // Supports "${...}".

  // browser.click, browser.type
  std::string selector;  // A CSS selector, resolved via document.querySelector.

  // browser.type
  std::string text;  // Supports "${...}".

  // browser.wait
  int wait_seconds = 0;

  // fail
  std::string fail_reason;

  // complete: map of output name -> expression (supports "${...}").
  std::map<std::string, std::string> outputs;
};

struct WorkflowInputSpec {
  std::string type;  // "string", "number", "boolean", "object" - informational only in V1.
  bool required = false;
};

struct WorkflowOutputSpec {
  std::string type;
};

enum class WorkflowStatus {
  kDraft,
  kPublished,
  kDeprecated,
};

std::string WorkflowStatusToString(WorkflowStatus status);
std::optional<WorkflowStatus> WorkflowStatusFromString(
    const std::string& status);

struct WorkflowDefinition {
  WorkflowDefinition();
  WorkflowDefinition(const WorkflowDefinition&);
  WorkflowDefinition& operator=(const WorkflowDefinition&);
  WorkflowDefinition(WorkflowDefinition&&);
  WorkflowDefinition& operator=(WorkflowDefinition&&);
  ~WorkflowDefinition();

  std::string id;
  std::string name;
  std::string description;
  std::string version = "1.0.0";
  WorkflowStatus status = WorkflowStatus::kDraft;

  std::map<std::string, WorkflowInputSpec> inputs;
  std::map<std::string, WorkflowOutputSpec> outputs;
  // Initial variable values, evaluated once at start (supports "${...}"
  // referencing inputs, but not other variables, to keep initialization
  // order-independent in V1).
  std::map<std::string, std::string> initial_variables;

  std::vector<WorkflowStep> steps;

  // Serializes to the JSON shape ParseWorkflowDefinition reads back.
  base::DictValue ToValue() const;
};

// A validation error found by ParseWorkflowDefinition or
// ValidateWorkflowDefinition, with enough context to show the user
// something actionable.
struct WorkflowValidationError {
  std::string step_id;  // Empty if not specific to one step.
  std::string message;
};

struct WorkflowParseResult {
  WorkflowParseResult();
  WorkflowParseResult(const WorkflowParseResult&);
  WorkflowParseResult& operator=(const WorkflowParseResult&);
  WorkflowParseResult(WorkflowParseResult&&);
  WorkflowParseResult& operator=(WorkflowParseResult&&);
  ~WorkflowParseResult();

  std::optional<WorkflowDefinition> definition;
  std::vector<WorkflowValidationError> errors;

  bool ok() const { return definition.has_value() && errors.empty(); }
};

// Parses and validates a workflow definition from its JSON representation
// (a base::DictValue already parsed from a JSON string, or parse it
// yourself with base::JSONReader first). Always runs full validation
// (unknown step types, dangling `next`/`on_true`/`on_false` references,
// duplicate step ids, missing `start` step) even when parsing otherwise
// succeeds, so `errors` may be non-empty even with `definition` set -
// callers should check ok() before treating a definition as runnable.
WorkflowParseResult ParseWorkflowDefinition(const base::DictValue& value);

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_DEFINITION_H_

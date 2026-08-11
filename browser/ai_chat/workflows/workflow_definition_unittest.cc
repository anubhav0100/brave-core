// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_definition.h"

#include "base/json/json_reader.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

// Parses `json_steps` (a JSON array literal string) as the `steps` of an
// otherwise-minimal, valid workflow, asserting it parses with no errors,
// and returns the resulting definition.
WorkflowDefinition ParseStepsOrDie(const std::string& json_steps) {
  std::string json = R"({
    "id": "wf_test",
    "name": "Test",
    "steps": )" + json_steps + "}";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  EXPECT_TRUE(parsed.has_value());
  WorkflowParseResult result = ParseWorkflowDefinition(*parsed);
  EXPECT_THAT(result.errors, testing::IsEmpty());
  EXPECT_TRUE(result.definition.has_value());
  return result.definition.value_or(WorkflowDefinition());
}

}  // namespace

TEST(WorkflowDefinitionStepTypeTest, NewTypesRoundTripThroughStrings) {
  const WorkflowStepType kNewTypes[] = {
      WorkflowStepType::kCallFlow, WorkflowStepType::kForEach,
      WorkflowStepType::kWhile,    WorkflowStepType::kUntil,
      WorkflowStepType::kBreak,    WorkflowStepType::kContinue,
      WorkflowStepType::kAiExtract, WorkflowStepType::kAiDecide,
  };
  for (auto type : kNewTypes) {
    std::string as_string = WorkflowStepTypeToString(type);
    EXPECT_FALSE(as_string.empty());
    auto round_tripped = WorkflowStepTypeFromString(as_string);
    ASSERT_TRUE(round_tripped.has_value());
    EXPECT_EQ(*round_tripped, type);
  }
}

TEST(WorkflowDefinitionStepTypeTest, AiActionIsNotYetSupported) {
  EXPECT_FALSE(WorkflowStepTypeFromString("ai.action").has_value());
  EXPECT_FALSE(WorkflowStepTypeFromString("tool.call").has_value());
  EXPECT_FALSE(WorkflowStepTypeFromString("webhook.call").has_value());
  EXPECT_FALSE(WorkflowStepTypeFromString("approval").has_value());
}

TEST(WorkflowDefinitionParseTest, CallFlowStepParsesAllFields) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "call"},
    {"id": "call", "type": "call_flow", "flow_id": "wf_child",
     "on_child_failure": "continue",
     "inputs": {"customer": "${var.name}"},
     "outputs": {"contract_number": "cn"},
     "next": "done"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  ASSERT_EQ(def.steps.size(), 3u);
  const WorkflowStep& call = def.steps[1];
  EXPECT_EQ(call.type, WorkflowStepType::kCallFlow);
  EXPECT_EQ(call.flow_id, "wf_child");
  EXPECT_EQ(call.on_child_failure, "continue");
  EXPECT_EQ(call.call_inputs.at("customer"), "${var.name}");
  EXPECT_EQ(call.call_outputs.at("contract_number"), "cn");
  EXPECT_EQ(call.next, "done");
}

TEST(WorkflowDefinitionParseTest, CallFlowRejectsBadOnChildFailure) {
  std::string json = R"({
    "id": "wf_test", "name": "Test",
    "steps": [
      {"id": "start", "type": "start", "next": "call"},
      {"id": "call", "type": "call_flow", "flow_id": "wf_child",
       "on_child_failure": "explode", "next": "done"},
      {"id": "done", "type": "complete", "outputs": {}}
    ]})";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(parsed.has_value());
  WorkflowParseResult result = ParseWorkflowDefinition(*parsed);
  EXPECT_FALSE(result.errors.empty());
}

TEST(WorkflowDefinitionParseTest, ForEachStepParsesAllFields) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "loop"},
    {"id": "loop", "type": "for_each", "items": "${var.records}",
     "item_variable": "record", "index_variable": "i",
     "body_start": "body", "max_iterations": 50, "next": "done"},
    {"id": "body", "type": "set_variable", "name": "x", "value": "y",
     "next": "loop"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  ASSERT_EQ(def.steps.size(), 4u);
  const WorkflowStep& loop = def.steps[1];
  EXPECT_EQ(loop.type, WorkflowStepType::kForEach);
  EXPECT_EQ(loop.items_expression, "${var.records}");
  EXPECT_EQ(loop.item_variable, "record");
  EXPECT_EQ(loop.index_variable, "i");
  EXPECT_EQ(loop.body_start, "body");
  EXPECT_EQ(loop.max_iterations, 50);
  EXPECT_EQ(loop.next, "done");
}

TEST(WorkflowDefinitionParseTest, WhileAndUntilStepsParse) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "w"},
    {"id": "w", "type": "while", "condition": "${var.x} == 1",
     "body_start": "body", "max_iterations": 10, "next": "u"},
    {"id": "body", "type": "set_variable", "name": "x", "value": "2",
     "next": "w"},
    {"id": "u", "type": "until", "condition": "${var.x} == 2",
     "body_start": "body2", "max_iterations": 10, "next": "done"},
    {"id": "body2", "type": "set_variable", "name": "x", "value": "2",
     "next": "u"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  const WorkflowStep& w = def.steps[1];
  EXPECT_EQ(w.type, WorkflowStepType::kWhile);
  EXPECT_EQ(w.loop_condition, "${var.x} == 1");
  EXPECT_EQ(w.body_start, "body");
  EXPECT_EQ(w.max_iterations, 10);

  const WorkflowStep& u = def.steps[3];
  EXPECT_EQ(u.type, WorkflowStepType::kUntil);
  EXPECT_EQ(u.loop_condition, "${var.x} == 2");
}

TEST(WorkflowDefinitionParseTest, BreakAndContinueNeedNoNext) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "loop"},
    {"id": "loop", "type": "for_each", "items": "${var.records}",
     "item_variable": "r", "body_start": "b", "next": "done"},
    {"id": "b", "type": "condition", "expression": "${var.r} == skip",
     "on_true": "cont", "on_false": "brk"},
    {"id": "cont", "type": "continue"},
    {"id": "brk", "type": "break"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  ASSERT_EQ(def.steps.size(), 6u);
  EXPECT_EQ(def.steps[3].type, WorkflowStepType::kContinue);
  EXPECT_EQ(def.steps[4].type, WorkflowStepType::kBreak);
}

TEST(WorkflowDefinitionParseTest, AiExtractStepParsesAllFields) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "extract"},
    {"id": "extract", "type": "ai.extract",
     "instruction": "Extract the customer name",
     "schema": "{\"name\": \"string\"}",
     "output_variable": "extracted", "next": "done"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  const WorkflowStep& step = def.steps[1];
  EXPECT_EQ(step.type, WorkflowStepType::kAiExtract);
  EXPECT_EQ(step.ai_instruction, "Extract the customer name");
  EXPECT_EQ(step.ai_schema_json, "{\"name\": \"string\"}");
  EXPECT_EQ(step.ai_output_variable, "extracted");
}

TEST(WorkflowDefinitionParseTest, AiDecideStepParsesAllFields) {
  WorkflowDefinition def = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "decide"},
    {"id": "decide", "type": "ai.decide",
     "instruction": "Is this a duplicate?",
     "allowed_outcomes": ["duplicate", "unique", "unclear"],
     "output_variable": "decision", "next": "done"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");
  const WorkflowStep& step = def.steps[1];
  EXPECT_EQ(step.type, WorkflowStepType::kAiDecide);
  EXPECT_EQ(step.ai_instruction, "Is this a duplicate?");
  EXPECT_THAT(step.allowed_outcomes,
             testing::ElementsAre("duplicate", "unique", "unclear"));
  EXPECT_EQ(step.ai_output_variable, "decision");
}

TEST(WorkflowDefinitionParseTest, AiDecideNeedsAtLeastTwoOutcomes) {
  std::string json = R"({
    "id": "wf_test", "name": "Test",
    "steps": [
      {"id": "start", "type": "start", "next": "decide"},
      {"id": "decide", "type": "ai.decide", "instruction": "x",
       "allowed_outcomes": ["only_one"], "output_variable": "d",
       "next": "done"},
      {"id": "done", "type": "complete", "outputs": {}}
    ]})";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(parsed.has_value());
  WorkflowParseResult result = ParseWorkflowDefinition(*parsed);
  EXPECT_FALSE(result.errors.empty());
}

TEST(WorkflowDefinitionParseTest, ForEachMissingBodyStartIsAnError) {
  std::string json = R"({
    "id": "wf_test", "name": "Test",
    "steps": [
      {"id": "start", "type": "start", "next": "loop"},
      {"id": "loop", "type": "for_each", "items": "${var.x}",
       "item_variable": "i", "next": "done"},
      {"id": "done", "type": "complete", "outputs": {}}
    ]})";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(parsed.has_value());
  WorkflowParseResult result = ParseWorkflowDefinition(*parsed);
  EXPECT_FALSE(result.errors.empty());
}

TEST(WorkflowDefinitionParseTest, ForEachDanglingBodyStartIsAnError) {
  std::string json = R"({
    "id": "wf_test", "name": "Test",
    "steps": [
      {"id": "start", "type": "start", "next": "loop"},
      {"id": "loop", "type": "for_each", "items": "${var.x}",
       "item_variable": "i", "body_start": "nonexistent", "next": "done"},
      {"id": "done", "type": "complete", "outputs": {}}
    ]})";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  ASSERT_TRUE(parsed.has_value());
  WorkflowParseResult result = ParseWorkflowDefinition(*parsed);
  EXPECT_FALSE(result.errors.empty());
}

TEST(WorkflowDefinitionToValueTest, CallFlowRoundTrips) {
  WorkflowDefinition original = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "call"},
    {"id": "call", "type": "call_flow", "flow_id": "wf_child",
     "on_child_failure": "continue",
     "inputs": {"customer": "${var.name}"},
     "outputs": {"contract_number": "cn"},
     "next": "done"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");

  base::DictValue value = original.ToValue();
  WorkflowParseResult reparsed = ParseWorkflowDefinition(value);
  ASSERT_TRUE(reparsed.ok());

  const WorkflowStep& call = reparsed.definition->steps[1];
  EXPECT_EQ(call.flow_id, "wf_child");
  EXPECT_EQ(call.on_child_failure, "continue");
  EXPECT_EQ(call.call_inputs.at("customer"), "${var.name}");
  EXPECT_EQ(call.call_outputs.at("contract_number"), "cn");
}

TEST(WorkflowDefinitionToValueTest, ForEachRoundTrips) {
  WorkflowDefinition original = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "loop"},
    {"id": "loop", "type": "for_each", "items": "${var.records}",
     "item_variable": "record", "index_variable": "i",
     "body_start": "body", "max_iterations": 50, "next": "done"},
    {"id": "body", "type": "set_variable", "name": "x", "value": "y",
     "next": "loop"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");

  base::DictValue value = original.ToValue();
  WorkflowParseResult reparsed = ParseWorkflowDefinition(value);
  ASSERT_TRUE(reparsed.ok());

  const WorkflowStep& loop = reparsed.definition->steps[1];
  EXPECT_EQ(loop.items_expression, "${var.records}");
  EXPECT_EQ(loop.item_variable, "record");
  EXPECT_EQ(loop.index_variable, "i");
  EXPECT_EQ(loop.max_iterations, 50);
}

TEST(WorkflowDefinitionToValueTest, AiDecideRoundTrips) {
  WorkflowDefinition original = ParseStepsOrDie(R"([
    {"id": "start", "type": "start", "next": "decide"},
    {"id": "decide", "type": "ai.decide", "instruction": "Is this a dup?",
     "allowed_outcomes": ["duplicate", "unique"],
     "output_variable": "decision", "next": "done"},
    {"id": "done", "type": "complete", "outputs": {}}
  ])");

  base::DictValue value = original.ToValue();
  WorkflowParseResult reparsed = ParseWorkflowDefinition(value);
  ASSERT_TRUE(reparsed.ok());

  const WorkflowStep& decide = reparsed.definition->steps[1];
  EXPECT_EQ(decide.ai_instruction, "Is this a dup?");
  EXPECT_THAT(decide.allowed_outcomes,
             testing::ElementsAre("duplicate", "unique"));
}

}  // namespace ai_chat

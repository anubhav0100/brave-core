// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_variable_store.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

class WorkflowVariableStoreOperatorTest : public testing::Test {
 protected:
  WorkflowVariableStore store_;
};

TEST_F(WorkflowVariableStoreOperatorTest, GreaterAndLessThanAreNumeric) {
  store_.SetVariable("x", "10");
  EXPECT_TRUE(store_.EvaluateCondition("${var.x} > 5"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.x} > 50"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.x} >= 10"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.x} < 50"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.x} < 5"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.x} <= 10"));
}

TEST_F(WorkflowVariableStoreOperatorTest, ContainsAndNotContains) {
  store_.SetVariable("text", "hello world");
  EXPECT_TRUE(store_.EvaluateCondition("${var.text} contains world"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.text} contains goodbye"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.text} not_contains goodbye"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.text} not_contains world"));
}

TEST_F(WorkflowVariableStoreOperatorTest, StartsWithAndEndsWith) {
  store_.SetVariable("text", "hello world");
  EXPECT_TRUE(store_.EvaluateCondition("${var.text} starts_with hello"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.text} starts_with world"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.text} ends_with world"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.text} ends_with hello"));
}

TEST_F(WorkflowVariableStoreOperatorTest, InAndNotIn) {
  store_.SetVariable("status", "duplicate");
  EXPECT_TRUE(
      store_.EvaluateCondition("${var.status} in duplicate,unique,unclear"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.status} in unique,unclear"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.status} not_in unique,unclear"));
  EXPECT_FALSE(
      store_.EvaluateCondition("${var.status} not_in duplicate,unique"));
}

TEST_F(WorkflowVariableStoreOperatorTest, InSupportsJsonArrayRightSide) {
  store_.SetVariable("status", "duplicate");
  EXPECT_TRUE(store_.EvaluateCondition(
      R"(${var.status} in ["duplicate", "unique"])"));
}

TEST_F(WorkflowVariableStoreOperatorTest, IsEmptyAndIsNotEmpty) {
  store_.SetVariable("empty_var", "");
  store_.SetVariable("full_var", "value");
  EXPECT_TRUE(store_.EvaluateCondition("${var.empty_var} is_empty"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.full_var} is_empty"));
  EXPECT_FALSE(store_.EvaluateCondition("${var.empty_var} is_not_empty"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.full_var} is_not_empty"));
}

TEST_F(WorkflowVariableStoreOperatorTest, ExistsAndNotExistsAliasEmptiness) {
  store_.SetVariable("defined", "value");
  EXPECT_TRUE(store_.EvaluateCondition("${var.defined} exists"));
  EXPECT_TRUE(store_.EvaluateCondition("${var.undefined} not_exists"));
}

TEST(WorkflowVariableStoreScopeTest, StepOutputWholeValueResolves) {
  WorkflowVariableStore store;
  base::DictValue output;
  output.Set("name", "Ada");
  output.Set("age", 30);
  store.SetStepOutput("extract", base::Value(std::move(output)));

  std::string resolved = store.Resolve("${step.extract.name}");
  EXPECT_EQ(resolved, "Ada");
}

TEST(WorkflowVariableStoreScopeTest, StepOutputMissingFieldResolvesEmpty) {
  WorkflowVariableStore store;
  base::DictValue output;
  output.Set("name", "Ada");
  store.SetStepOutput("extract", base::Value(std::move(output)));

  EXPECT_EQ(store.Resolve("${step.extract.missing_field}"), "");
  EXPECT_EQ(store.Resolve("${step.unknown_step.name}"), "");
}

TEST(WorkflowVariableStoreScopeTest, LoopItemAndIndexResolve) {
  WorkflowVariableStore store;
  store.SetLoopItem(base::Value("customer_1"));
  store.SetLoopIndex(3);

  EXPECT_EQ(store.Resolve("${loop.item}"), "customer_1");
  EXPECT_EQ(store.Resolve("${loop.index}"), "3");
}

TEST(WorkflowVariableStoreScopeTest, ClearLoopBindingsResolvesEmpty) {
  WorkflowVariableStore store;
  store.SetLoopItem(base::Value("customer_1"));
  store.SetLoopIndex(3);
  store.ClearLoopBindings();

  EXPECT_EQ(store.Resolve("${loop.item}"), "");
  EXPECT_EQ(store.Resolve("${loop.index}"), "");
}

TEST(WorkflowVariableStoreScopeTest, EqualsAndNotEqualsStillWork) {
  WorkflowVariableStore store;
  store.SetVariable("x", "5");
  EXPECT_TRUE(store.EvaluateCondition("${var.x} == 5"));
  EXPECT_FALSE(store.EvaluateCondition("${var.x} == 6"));
  EXPECT_TRUE(store.EvaluateCondition("${var.x} != 6"));
}

}  // namespace ai_chat

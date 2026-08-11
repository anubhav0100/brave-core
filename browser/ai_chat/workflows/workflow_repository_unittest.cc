// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_repository.h"

#include "base/json/json_reader.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

namespace {

// A minimal, valid, unique workflow with one call_flow step to `target`
// (or none if `target` is empty), for cycle-detection tests where the step
// graph itself doesn't matter, only the call_flow reference does.
base::DictValue MakeWorkflow(const std::string& id,
                             const std::string& target) {
  std::string steps_json = target.empty()
      ? R"([{"id": "s", "type": "start", "next": "c"},
            {"id": "c", "type": "complete", "outputs": {}}])"
      : R"([{"id": "s", "type": "start", "next": "call"},
            {"id": "call", "type": "call_flow", "flow_id": ")" +
            target + R"(", "next": "c"},
            {"id": "c", "type": "complete", "outputs": {}}])";
  std::string json = R"({"id": ")" + id + R"(", "name": ")" + id +
                     R"(", "steps": )" + steps_json + "}";
  auto parsed = base::JSONReader::ReadDict(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  return parsed ? std::move(*parsed) : base::DictValue();
}

}  // namespace

class WorkflowRepositoryTest : public testing::Test {
 protected:
  WorkflowRepositoryTest() {
    WorkflowRepository::RegisterProfilePrefs(prefs_.registry());
  }

  TestingPrefServiceSimple prefs_;
  WorkflowRepository repository_{&prefs_};
};

TEST_F(WorkflowRepositoryTest, SavesAndRetrievesAWorkflowWithNoCallFlow) {
  auto result = repository_.SaveWorkflow(MakeWorkflow("wf_a", ""));
  EXPECT_TRUE(result.id.has_value());
  EXPECT_THAT(result.errors, testing::IsEmpty());
  EXPECT_TRUE(repository_.GetWorkflow("wf_a").has_value());
}

TEST_F(WorkflowRepositoryTest, AcceptsANonCyclicChain) {
  // A -> B -> C -> D, saved in dependency order so each target already
  // exists (SaveWorkflow's cycle check only needs to resolve targets that
  // are already saved - the workflow being saved itself is checked via its
  // own in-memory definition, not a lookup).
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_d", "")).errors,
             testing::IsEmpty());
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_c", "wf_d")).errors,
             testing::IsEmpty());
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_b", "wf_c")).errors,
             testing::IsEmpty());
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b")).errors,
             testing::IsEmpty());
}

TEST_F(WorkflowRepositoryTest, AcceptsAWorkflowCallingAnotherThatDoesNotCallBack) {
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_b", "")).errors,
             testing::IsEmpty());
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b")).errors,
             testing::IsEmpty());
  // Re-saving wf_a (e.g. an edit) with the same single call_flow target is
  // still fine - not a cycle just because it's saved more than once.
  EXPECT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b")).errors,
             testing::IsEmpty());
}

TEST_F(WorkflowRepositoryTest, RejectsADirectCycle) {
  // A -> A.
  auto result = repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_a"));
  EXPECT_FALSE(result.id.has_value());
  EXPECT_FALSE(result.errors.empty());
  EXPECT_FALSE(repository_.GetWorkflow("wf_a").has_value());
}

TEST_F(WorkflowRepositoryTest, RejectsATwoWorkflowCycle) {
  // B already exists and calls A; saving A to call B would close the loop.
  ASSERT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_b", "wf_a")).errors,
             testing::IsEmpty());
  auto result = repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b"));
  EXPECT_FALSE(result.id.has_value());
  EXPECT_FALSE(result.errors.empty());
}

TEST_F(WorkflowRepositoryTest, RejectsAThreeWorkflowCycle) {
  // B calls C, C calls A (not yet saved); saving A to call B closes the
  // A -> B -> C -> A loop.
  ASSERT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_c", "wf_a")).errors,
             testing::IsEmpty());
  ASSERT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_b", "wf_c")).errors,
             testing::IsEmpty());
  auto result = repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b"));
  EXPECT_FALSE(result.id.has_value());
  EXPECT_FALSE(result.errors.empty());
}

TEST_F(WorkflowRepositoryTest, GetDependenciesAndDependents) {
  ASSERT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_b", "")).errors,
             testing::IsEmpty());
  ASSERT_THAT(repository_.SaveWorkflow(MakeWorkflow("wf_a", "wf_b")).errors,
             testing::IsEmpty());

  EXPECT_THAT(repository_.GetDependencies("wf_a"),
             testing::ElementsAre("wf_b"));
  EXPECT_THAT(repository_.GetDependencies("wf_b"), testing::IsEmpty());
  EXPECT_THAT(repository_.GetDependents("wf_b"), testing::ElementsAre("wf_a"));
  EXPECT_THAT(repository_.GetDependents("wf_a"), testing::IsEmpty());
}

}  // namespace ai_chat

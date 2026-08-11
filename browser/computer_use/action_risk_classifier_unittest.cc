// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/action_risk_classifier.h"

#include "brave/browser/computer_use/computer_use_session_state.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace computer_use {

// Deliberately doesn't exercise GetProcessNameAtPoint()/
// GetForegroundProcessName() themselves - those need real interactive
// desktop/window-station access this automated test environment doesn't
// have (same limitation as the desktop capture path - see
// brave-ai-computer-use.md's Phase 1/2 Status notes). Tests
// ClassifyDesktopAction() directly with hand-picked process names instead,
// since that's where the actual risk logic lives.
class ActionRiskClassifierTest : public testing::Test {
 protected:
  ActionRiskClassifierTest() {
    ComputerUseSessionState::RegisterProfilePrefs(prefs_.registry());
  }

  TestingPrefServiceSimple prefs_;
  ComputerUseSessionState state_{&prefs_};
};

TEST_F(ActionRiskClassifierTest, SensitiveProcessIsAlwaysRisky) {
  state_.MarkAppInteracted("cmd.exe");  // Even if already "known".

  auto result = ClassifyDesktopAction("cmd.exe", "", &state_);

  EXPECT_TRUE(result.is_risky);
  EXPECT_THAT(result.reason, testing::HasSubstr("sensitive system utility"));
}

TEST_F(ActionRiskClassifierTest, SensitiveKeywordInActionDetailIsRisky) {
  state_.MarkAppInteracted("notepad.exe");

  auto result =
      ClassifyDesktopAction("notepad.exe", "please format C: now", &state_);

  EXPECT_TRUE(result.is_risky);
  EXPECT_THAT(result.reason, testing::HasSubstr("sensitive keyword"));
}

TEST_F(ActionRiskClassifierTest, FirstActionAgainstNewAppIsRisky) {
  auto result = ClassifyDesktopAction("notepad.exe", "hello world", &state_);

  EXPECT_TRUE(result.is_risky);
  EXPECT_THAT(result.reason, testing::HasSubstr("hasn't used yet"));
}

TEST_F(ActionRiskClassifierTest, KnownAppWithBenignContentIsNotRisky) {
  state_.MarkAppInteracted("notepad.exe");

  auto result = ClassifyDesktopAction("notepad.exe", "hello world", &state_);

  EXPECT_FALSE(result.is_risky);
  EXPECT_TRUE(result.reason.empty());
}

TEST_F(ActionRiskClassifierTest, EmptyProcessNameSkipsNewAppCheck) {
  // Can't determine the target window - shouldn't crash or spuriously
  // flag risk purely from not knowing the process name.
  auto result = ClassifyDesktopAction("", "hello world", &state_);

  EXPECT_FALSE(result.is_risky);
}

}  // namespace computer_use

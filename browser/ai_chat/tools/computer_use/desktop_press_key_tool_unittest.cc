// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/desktop_press_key_tool.h"

#include <memory>

#include "brave/browser/computer_use/action_risk_classifier.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

// Exercises DesktopInputToolBase's shared consent + risk gate (see
// desktop_input_tool_base.h) via one concrete tool. Unlike desktop
// screenshot capture, GetForegroundProcessName() *can* resolve a real
// window even in this automated test environment (GetForegroundWindow()
// is a much lower-privilege API than the desktop-duplication APIs capture
// needs) - so whichever process actually has focus when the test runs
// would otherwise be flagged risky as "an app the AI hasn't used yet this
// session." Tests that don't care about that pre-mark it interacted via
// the session state directly, matching what a real conversation would do
// after the *first* risky confirmation for that app.
class DesktopPressKeyToolTest : public testing::Test {
 protected:
  DesktopPressKeyToolTest()
      : task_environment_(content::BrowserTaskEnvironment::IO_MAINLOOP) {}

  std::unique_ptr<DesktopPressKeyTool> CreateTool() {
    return std::make_unique<DesktopPressKeyTool>(&profile_);
  }

  mojom::ToolUseEvent MakeEvent(const std::string& id,
                                const std::string& arguments_json) {
    return mojom::ToolUseEvent("desktop_press_key", id, arguments_json,
                               std::nullopt, std::nullopt, nullptr, false);
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(DesktopPressKeyToolTest, FirstUseAlwaysChallenges) {
  auto tool = CreateTool();
  auto event = MakeEvent("1", R"({"key": "Enter"})");

  auto result = tool->RequiresUserInteractionBeforeHandling(event);

  ASSERT_TRUE(std::holds_alternative<mojom::PermissionChallengePtr>(result));
  EXPECT_TRUE(std::get<mojom::PermissionChallengePtr>(result));
}

TEST_F(DesktopPressKeyToolTest, BenignKeyAfterConsentDoesNotChallenge) {
  auto tool = CreateTool();
  tool->UserPermissionGranted("1");
  // Pre-mark whatever's actually focused as already-interacted, so this
  // test doesn't depend on what window happens to have focus when it runs.
  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      &profile_)
      ->MarkAppInteracted(computer_use::GetForegroundProcessName());

  auto event = MakeEvent("2", R"({"key": "Enter"})");
  auto result = tool->RequiresUserInteractionBeforeHandling(event);

  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_FALSE(std::get<bool>(result));
}

TEST_F(DesktopPressKeyToolTest, RiskyKeyStillChallengesAfterConsent) {
  auto tool = CreateTool();
  tool->UserPermissionGranted("1");

  // "Delete" matches the sensitive-keyword list regardless of target app.
  auto event = MakeEvent("2", R"({"key": "Delete"})");
  auto result = tool->RequiresUserInteractionBeforeHandling(event);

  ASSERT_TRUE(std::holds_alternative<mojom::PermissionChallengePtr>(result));
  EXPECT_TRUE(std::get<mojom::PermissionChallengePtr>(result));
}

TEST_F(DesktopPressKeyToolTest, IsAgentTool) {
  auto tool = CreateTool();
  EXPECT_TRUE(tool->IsAgentTool());
}

}  // namespace ai_chat

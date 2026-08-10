// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/get_desktop_screenshot_tool.h"

#include <memory>
#include <string>

#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ai_chat {

// Note: the actual OS-level capture path (DesktopCaptureSession ->
// webrtc::DesktopCapturer, via content::desktop_capture::CreateScreenCapturer)
// is deliberately NOT exercised end-to-end here. It requires real
// interactive desktop/window-station access (both the DirectX Desktop
// Duplication API and its GDI BitBlt fallback need one), which automated/CI
// process contexts commonly don't have - the same reason
// full_screenshotter_unittest.cc mocks its capture backend rather than
// driving a real compositor. Verified manually instead: see
// brave-ai-computer-use.md's Status section.
class GetDesktopScreenshotToolTest : public testing::Test {
 protected:
  GetDesktopScreenshotToolTest()
      : task_environment_(content::BrowserTaskEnvironment::IO_MAINLOOP) {}

  std::unique_ptr<GetDesktopScreenshotTool> CreateTool() {
    return std::make_unique<GetDesktopScreenshotTool>(&profile_);
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(GetDesktopScreenshotToolTest, RequiresUserInteractionBeforeHandling) {
  auto tool = CreateTool();
  mojom::ToolUseEvent event(tool->Name().data(), "1", "{}", std::nullopt,
                            std::nullopt, nullptr, false);

  // Before permission is granted: returns a PermissionChallenge, since
  // handing the entire desktop's contents to the model is exactly the kind
  // of sensitive read HistorySearchTool's identical pattern exists for.
  auto result = tool->RequiresUserInteractionBeforeHandling(event);
  ASSERT_TRUE(std::holds_alternative<mojom::PermissionChallengePtr>(result));
  EXPECT_TRUE(std::get<mojom::PermissionChallengePtr>(result));

  // After permission is granted: no further interaction needed for the
  // rest of the conversation.
  tool->UserPermissionGranted("1");
  result = tool->RequiresUserInteractionBeforeHandling(event);
  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_FALSE(std::get<bool>(result));
}

TEST_F(GetDesktopScreenshotToolTest, IsAgentTool) {
  auto tool = CreateTool();
  EXPECT_TRUE(tool->IsAgentTool());
}

}  // namespace ai_chat

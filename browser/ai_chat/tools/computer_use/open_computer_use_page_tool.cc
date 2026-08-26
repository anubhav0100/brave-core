// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/open_computer_use_page_tool.h"

#include <utility>

#include "brave/browser/ai_chat/tools/tab_utils.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace ai_chat {

OpenComputerUsePageTool::OpenComputerUsePageTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

OpenComputerUsePageTool::~OpenComputerUsePageTool() = default;

std::string_view OpenComputerUsePageTool::Name() const {
  return "open_computer_use_page";
}

std::string_view OpenComputerUsePageTool::Description() const {
  return "Opens chrome://computer-use - this profile's computer-use status "
        "page (live screenshot, RDP session/history, and the \"always "
        "allow AI screenshot access\" setting). Use this whenever the user "
        "asks to see, open, or check the computer-use page.";
}

void OpenComputerUsePageTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  GURL url(kComputerUseURL);
  Profile* profile = Profile::FromBrowserContext(browser_context_);
  if (profile && FindAndActivateExistingTab(profile, url)) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "chrome://computer-use was already open - switched to its tab."),
        {});
    return;
  }

  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no open browser window to open a "
                                  "tab in."),
        {});
    return;
  }
  web_contents->OpenURL(
      {url, content::Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
       ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false},
      /*navigation_handle_callback=*/{});
  std::move(callback).Run(
      CreateContentBlocksForText("Opened chrome://computer-use."), {});
}

}  // namespace ai_chat

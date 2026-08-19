// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_TAB_UTILS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_TAB_UTILS_H_

class GURL;
class Profile;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace ai_chat {

// Looks across all of `profile`'s browser windows for a tab already showing
// `url`, and if found, activates it (and its window) and returns true.
// Returns false if no matching tab exists - callers should open a new one in
// that case. Used by tools like open_n8n/open_delegation so repeated calls
// switch to the existing tab instead of piling up duplicates.
bool FindAndActivateExistingTab(Profile* profile, const GURL& url);

// The WebContents for the active tab of `browser_context`'s frontmost
// tabbed browser window, or nullptr if there isn't one (no window open,
// context isn't a real profile, etc.). Used by tools that operate on
// "whatever the user is currently looking at" rather than a URL/tab id the
// model passed in.
content::WebContents* GetActiveWebContentsFor(
    content::BrowserContext* browser_context);

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_TAB_UTILS_H_

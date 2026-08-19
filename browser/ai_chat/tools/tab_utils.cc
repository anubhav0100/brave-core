// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/tab_utils.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace ai_chat {

bool FindAndActivateExistingTab(Profile* profile, const GURL& url) {
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile) {
      continue;
    }
    TabStripModel* tab_strip = browser->GetTabStripModel();
    if (!tab_strip) {
      continue;
    }
    for (int i = 0; i < tab_strip->count(); ++i) {
      tabs::TabInterface* tab = tab_strip->GetTabAtIndex(i);
      if (!tab || !tab->GetContents()) {
        continue;
      }
      if (tab->GetContents()->GetURL() == url) {
        browser->GetWindow()->Activate();
        tab_strip->ActivateTabAt(i);
        return true;
      }
    }
  }
  return false;
}

content::WebContents* GetActiveWebContentsFor(
    content::BrowserContext* browser_context) {
  Profile* profile = Profile::FromBrowserContext(browser_context);
  if (!profile) {
    return nullptr;
  }
  BrowserWindowInterface* browser =
      ProfileBrowserCollection::GetForProfile(profile)->FindTabbedBrowser();
  if (!browser) {
    return nullptr;
  }
  tabs::TabInterface* tab = browser->GetActiveTabInterface();
  return tab ? tab->GetContents() : nullptr;
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/response_memory_session.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"

namespace ai_chat {

namespace {

void AppendParagraph(base::ListValue& paragraphs,
                     const std::string& text,
                     int heading_level) {
  if (text.empty()) {
    return;
  }
  base::DictValue paragraph;
  paragraph.Set("text", text);
  paragraph.Set("heading_level", heading_level);
  paragraphs.Append(std::move(paragraph));
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

}  // namespace

ResponseMemorySession::ResponseMemorySession(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ResponseMemorySession::~ResponseMemorySession() = default;

void ResponseMemorySession::AddResponse(const std::string& label,
                                        const std::string& text) {
  entries_.push_back({label, text});

  if (auto* prefs = browser_context_
                        ? user_prefs::UserPrefs::Get(browser_context_)
                        : nullptr;
      prefs && AiChatContentIndex::IsEnabledForProfile(prefs)) {
    if (auto* index =
            AiChatContentIndexFactory::GetForBrowserContext(browser_context_)) {
      index->IndexChunks("response", label, "", {text});
    }
  }
}

void ResponseMemorySession::SaveAsWordDocument(const std::string& filename,
                                               ResultCallback callback) {
  if (entries_.empty()) {
    std::move(callback).Run(false, "Nothing saved to memory yet.");
    return;
  }

  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        false, "No open browser window to show the Save As dialog in.");
    return;
  }

  base::ListValue paragraphs;
  for (const auto& entry : entries_) {
    AppendParagraph(paragraphs, entry.label, 1);
    AppendParagraph(paragraphs, entry.text, 0);
  }

  std::string document_filename = base::StrCat({filename, ".docx"});
  std::vector<OoxmlPart> parts;
  parts.push_back({"[Content_Types].xml", kWordContentTypesXml});
  parts.push_back({"_rels/.rels", kWordRootRelsXml});
  parts.push_back({"word/document.xml", BuildWordDocumentXml(paragraphs)});

  BuildOoxmlArchiveAndSaveAs(
      web_contents, document_filename, std::move(parts),
      base::BindOnce(&ResponseMemorySession::OnSaveComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     document_filename));
}

void ResponseMemorySession::OnSaveComplete(ResultCallback callback,
                                           std::string filename,
                                           DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        false, base::StrCat({"Error: failed to save '", filename, "': ",
                             result.error_message}));
    return;
  }
  std::move(callback).Run(
      true, base::StrCat({"Saved '", filename, "' with ",
                          base::NumberToString(entries_.size()),
                          " saved response(s)."}));
}

void ResponseMemorySession::Clear() {
  entries_.clear();
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/read_word_document_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"

namespace ai_chat {

ReadWordDocumentTool::ReadWordDocumentTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ReadWordDocumentTool::~ReadWordDocumentTool() = default;

std::string_view ReadWordDocumentTool::Name() const {
  return mojom::kReadWordDocumentToolName;
}

std::string_view ReadWordDocumentTool::Description() const {
  return "Ask the user to pick an existing Word (.docx) document via a "
         "native Open dialog, then return its plain text. Use this when the "
         "user wants you to edit or update a document they already have: "
         "read it with this tool, discuss the changes with the user, then "
         "call create_word_document with the full updated paragraph list "
         "(existing content plus your changes) and have the user pick the "
         "same file in the Save As dialog to overwrite it. Only extracts "
         "plain text - tables, images, and rich formatting are not read.";
}

void ReadWordDocumentTool::UseTool(const std::string& input_json,
                                   UseToolCallback callback) {
  content::WebContents* web_contents = nullptr;
  if (Profile* profile = Profile::FromBrowserContext(browser_context_)) {
    if (BrowserWindowInterface* browser =
            ProfileBrowserCollection::GetForProfile(profile)
                ->FindTabbedBrowser()) {
      if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
        web_contents = tab->GetContents();
      }
    }
  }
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to show the Open dialog in."),
        {});
    return;
  }

  ShowOpenDialogAndReadWordDocumentText(
      web_contents,
      base::BindOnce(&ReadWordDocumentTool::OnTextRead,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ReadWordDocumentTool::OnTextRead(UseToolCallback callback,
                                      std::optional<std::string> text) {
  if (!text) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "The user canceled the Open dialog, or the chosen file couldn't "
            "be read as a Word document."),
        {});
    return;
  }

  if (auto* prefs = browser_context_
                        ? user_prefs::UserPrefs::Get(browser_context_)
                        : nullptr;
      prefs && AiChatContentIndex::IsEnabledForProfile(prefs)) {
    if (auto* index = AiChatContentIndexFactory::GetForBrowserContext(
            browser_context_)) {
      // No filename available from the Open dialog result - use the
      // document's own first line as a label instead of touching the
      // shared dialog helper's signature for every other caller.
      std::vector<std::string> lines = base::SplitString(
          *text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      std::string first_line = lines.empty() ? "" : lines[0];
      if (first_line.size() > 80) {
        first_line.resize(80);
      }
      index->IndexChunks(
          "document",
          first_line.empty() ? "Opened Word document" : first_line, "",
          lines);
    }
  }

  std::move(callback).Run(CreateContentBlocksForText(*text), {});
}

}  // namespace ai_chat

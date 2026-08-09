// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/index_local_file_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_factory.h"
#include "brave/browser/ai_chat/text_file_extractor.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace ai_chat {

namespace {

// Presents a native "Open" dialog restricted to common text-like file
// types, then reports back which path (if any) the user chose.
// Self-deleting, mirroring OpenDocxDialog/SaveAsDialog in
// document_download_util.cc.
class OpenTextFileDialog : public ui::SelectFileDialog::Listener {
 public:
  using PathCallback = base::OnceCallback<void(base::FilePath)>;

  static void Show(content::WebContents* web_contents, PathCallback callback) {
    new OpenTextFileDialog(web_contents, std::move(callback));
  }

  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    std::move(callback_).Run(file.path());
    delete this;
  }

  void FileSelectionCanceled() override {
    std::move(callback_).Run(base::FilePath());
    delete this;
  }

 private:
  explicit OpenTextFileDialog(content::WebContents* web_contents,
                              PathCallback callback)
      : callback_(std::move(callback)),
        dialog_(ui::SelectFileDialog::Create(
            this,
            std::make_unique<ChromeSelectFilePolicy>(web_contents))) {
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions.resize(1);
    for (const char* ext :
        {"txt", "md", "csv", "json", "log", "html", "htm", "xml"}) {
      file_type_info.extensions[0].push_back(
          base::FilePath::FromASCII(ext).value());
    }
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
        base::FilePath(), &file_type_info, 0, FILE_PATH_LITERAL("txt"),
        platform_util::GetTopLevel(web_contents->GetNativeView()));
  }

  ~OpenTextFileDialog() override { dialog_->ListenerDestroyed(); }

  PathCallback callback_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

}  // namespace

IndexLocalFileTool::IndexLocalFileTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

IndexLocalFileTool::~IndexLocalFileTool() = default;

std::string_view IndexLocalFileTool::Name() const {
  return "index_local_file";
}

std::string_view IndexLocalFileTool::Description() const {
  return "Ask the user to pick a local text-like file (.txt, .md, .csv, "
         ".json, .log, .html, .xml) via a native Open dialog, then index "
         "its content into this profile's on-device content index so "
         "search_indexed_content can find it later, and return the "
         "extracted text. For .docx files, use read_word_document instead. "
         "Use this when the user wants you to remember/search a local file "
         "that isn't already part of this conversation.";
}

void IndexLocalFileTool::UseTool(const std::string& input_json,
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

  OpenTextFileDialog::Show(
      web_contents, base::BindOnce(&IndexLocalFileTool::OnFileChosen,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(callback)));
}

void IndexLocalFileTool::OnFileChosen(UseToolCallback callback,
                                      base::FilePath path) {
  if (path.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("The user canceled the Open dialog."), {});
    return;
  }
  extractor_ = std::make_unique<TextFileExtractor>();
  extractor_->ExtractText(
      browser_context_, path,
      base::BindOnce(&IndexLocalFileTool::OnTextExtracted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     path.BaseName().AsUTF8Unsafe()));
}

void IndexLocalFileTool::OnTextExtracted(UseToolCallback callback,
                                         std::string label,
                                         std::optional<std::string> text) {
  extractor_.reset();
  if (!text || text->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Couldn't extract any text from the chosen file."),
        {});
    return;
  }

  if (auto* prefs = browser_context_
                        ? user_prefs::UserPrefs::Get(browser_context_)
                        : nullptr;
      prefs && AiChatContentIndex::IsEnabledForProfile(prefs)) {
    if (auto* index = AiChatContentIndexFactory::GetForBrowserContext(
            browser_context_)) {
      std::vector<std::string> lines = base::SplitString(
          *text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      index->IndexChunks("file", label, "", lines);
    }
  }

  std::move(callback).Run(CreateContentBlocksForText(*text), {});
}

}  // namespace ai_chat

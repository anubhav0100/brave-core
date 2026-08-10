// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/n8n_sync_tools.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "brave/browser/n8n/n8n_process_manager.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace ai_chat {

namespace {

content::WebContents* FindActiveWebContents(
    content::BrowserContext* browser_context) {
  if (Profile* profile = Profile::FromBrowserContext(browser_context)) {
    if (BrowserWindowInterface* browser =
            ProfileBrowserCollection::GetForProfile(profile)
                ->FindTabbedBrowser()) {
      if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
        return tab->GetContents();
      }
    }
  }
  return nullptr;
}

// Presents a native "Save As" dialog restricted to .zip, defaulting to a
// dated filename. Self-deleting, mirroring the pattern in
// document_download_util.cc.
class SaveBackupAsDialog : public ui::SelectFileDialog::Listener {
 public:
  using PathCallback = base::OnceCallback<void(base::FilePath)>;

  static void Show(content::WebContents* web_contents,
                   PathCallback callback) {
    new SaveBackupAsDialog(web_contents, std::move(callback));
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
  explicit SaveBackupAsDialog(content::WebContents* web_contents,
                              PathCallback callback)
      : callback_(std::move(callback)),
        dialog_(ui::SelectFileDialog::Create(
            this,
            std::make_unique<ChromeSelectFilePolicy>(web_contents))) {
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions.resize(1);
    file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("zip"));
    std::string default_name = base::StrCat(
        {"n8n_backup_export_",
         base::NumberToString(
             base::Time::Now().InMillisecondsSinceUnixEpoch()),
         ".zip"});
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(),
        base::FilePath::FromASCII(default_name), &file_type_info, 0,
        FILE_PATH_LITERAL("zip"),
        platform_util::GetTopLevel(web_contents->GetNativeView()));
  }

  ~SaveBackupAsDialog() override { dialog_->ListenerDestroyed(); }

  PathCallback callback_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

// Presents a native "Open" dialog restricted to .zip. Self-deleting.
class OpenBackupDialog : public ui::SelectFileDialog::Listener {
 public:
  using PathCallback = base::OnceCallback<void(base::FilePath)>;

  static void Show(content::WebContents* web_contents,
                   PathCallback callback) {
    new OpenBackupDialog(web_contents, std::move(callback));
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
  explicit OpenBackupDialog(content::WebContents* web_contents,
                            PathCallback callback)
      : callback_(std::move(callback)),
        dialog_(ui::SelectFileDialog::Create(
            this,
            std::make_unique<ChromeSelectFilePolicy>(web_contents))) {
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions.resize(1);
    file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("zip"));
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
        base::FilePath(), &file_type_info, 0, FILE_PATH_LITERAL("zip"),
        platform_util::GetTopLevel(web_contents->GetNativeView()));
  }

  ~OpenBackupDialog() override { dialog_->ListenerDestroyed(); }

  PathCallback callback_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

}  // namespace

// ExportN8nBackupTool ------------------------------------------------------

ExportN8nBackupTool::ExportN8nBackupTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {}

ExportN8nBackupTool::~ExportN8nBackupTool() = default;

std::string_view ExportN8nBackupTool::Name() const {
  return "export_n8n_backup";
}

std::string_view ExportN8nBackupTool::Description() const {
  return "Asks the user where to save a copy of the most recent local n8n "
         "backup (via a native Save dialog) - use this when the user "
         "wants to move their n8n flows to another machine. They can copy "
         "the saved .zip file there (a cloud-synced folder, USB drive, "
         "email, etc.) and import it with import_n8n_backup.";
}

void ExportN8nBackupTool::UseTool(const std::string& input_json,
                                  UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  content::WebContents* web_contents =
      FindActiveWebContents(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to show the Save dialog in."),
        {});
    return;
  }
  SaveBackupAsDialog::Show(
      web_contents, base::BindOnce(&ExportN8nBackupTool::OnPathChosen,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(callback)));
}

void ExportN8nBackupTool::OnPathChosen(UseToolCallback callback,
                                       base::FilePath path) {
  if (path.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("The user canceled the Save dialog."),
        {});
    return;
  }
  manager_->ExportLatestBackupToFile(
      path, base::BindOnce(&ExportN8nBackupTool::OnExportComplete,
                           weak_ptr_factory_.GetWeakPtr(),
                           std::move(callback)));
}

void ExportN8nBackupTool::OnExportComplete(UseToolCallback callback,
                                           bool success,
                                           std::string message) {
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({success ? "" : "Error: ", message})),
      {});
}

// ImportN8nBackupTool ------------------------------------------------------

ImportN8nBackupTool::ImportN8nBackupTool(
    N8nProcessManager* manager,
    content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {}

ImportN8nBackupTool::~ImportN8nBackupTool() = default;

std::string_view ImportN8nBackupTool::Name() const {
  return "import_n8n_backup";
}

std::string_view ImportN8nBackupTool::Description() const {
  return "Asks the user to pick a previously-exported n8n backup .zip "
         "file (via a native Open dialog - see export_n8n_backup) and "
         "restores it into this machine's n8n data, bringing over flows "
         "created on another machine. n8n must not currently be running - "
         "ask the user to stop it first if it is.";
}

void ImportN8nBackupTool::UseTool(const std::string& input_json,
                                  UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: n8n isn't available."), {});
    return;
  }
  content::WebContents* web_contents =
      FindActiveWebContents(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to show the Open dialog in."),
        {});
    return;
  }
  OpenBackupDialog::Show(
      web_contents, base::BindOnce(&ImportN8nBackupTool::OnPathChosen,
                                   weak_ptr_factory_.GetWeakPtr(),
                                   std::move(callback)));
}

void ImportN8nBackupTool::OnPathChosen(UseToolCallback callback,
                                       base::FilePath path) {
  if (path.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("The user canceled the Open dialog."),
        {});
    return;
  }
  manager_->ImportBackupFromFile(
      path, base::BindOnce(&ImportN8nBackupTool::OnImportComplete,
                           weak_ptr_factory_.GetWeakPtr(),
                           std::move(callback)));
}

void ImportN8nBackupTool::OnImportComplete(UseToolCallback callback,
                                           bool success,
                                           std::string message) {
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({success ? "" : "Error: ", message})),
      {});
}

}  // namespace ai_chat

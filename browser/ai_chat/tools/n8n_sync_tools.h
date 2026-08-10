// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_SYNC_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_SYNC_TOOLS_H_

#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class N8nProcessManager;

// "Multi-machine sync" for n8n flows without any dedicated sync backend:
// asks the user (via a native Save As dialog) where to save a copy of the
// most recent local n8n backup, so they can move it to another machine
// (a cloud-synced folder, a USB drive, email, whatever) and import it
// there with import_n8n_backup. See
// N8nProcessManager::ExportLatestBackupToFile for the actual copy logic.
class ExportN8nBackupTool : public Tool {
 public:
  ExportN8nBackupTool(N8nProcessManager* manager,
                      content::BrowserContext* browser_context);
  ~ExportN8nBackupTool() override;

  ExportN8nBackupTool(const ExportN8nBackupTool&) = delete;
  ExportN8nBackupTool& operator=(const ExportN8nBackupTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnPathChosen(UseToolCallback callback, base::FilePath path);
  void OnExportComplete(UseToolCallback callback,
                        bool success,
                        std::string message);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<ExportN8nBackupTool> weak_ptr_factory_{this};
};

// The counterpart to ExportN8nBackupTool: asks the user (via a native
// Open dialog) to pick a backup zip file - typically one exported from
// another machine - and restores it into this machine's n8n data
// directory. Refuses while n8n is currently running.
class ImportN8nBackupTool : public Tool {
 public:
  ImportN8nBackupTool(N8nProcessManager* manager,
                      content::BrowserContext* browser_context);
  ~ImportN8nBackupTool() override;

  ImportN8nBackupTool(const ImportN8nBackupTool&) = delete;
  ImportN8nBackupTool& operator=(const ImportN8nBackupTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnPathChosen(UseToolCallback callback, base::FilePath path);
  void OnImportComplete(UseToolCallback callback,
                        bool success,
                        std::string message);

  raw_ptr<N8nProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<ImportN8nBackupTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_N8N_SYNC_TOOLS_H_

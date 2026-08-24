// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_SCHEDULED_TASK_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_SCHEDULED_TASK_TOOLS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

// Creates a new unattended scheduled task: at the given time (once, daily,
// or weekly), the browser will submit `prompt` as a new AI Chat
// conversation entirely on its own - no person needs to be present. Because
// nobody is there to click "Allow" on a permission challenge, the task may
// only use the tools explicitly named in `tool_allowlist` while running;
// any other tool call it attempts is refused automatically. See
// ScheduledTaskService for the full unattended-execution model.
class CreateScheduledAiTaskTool : public Tool {
 public:
  explicit CreateScheduledAiTaskTool(content::BrowserContext* browser_context);
  ~CreateScheduledAiTaskTool() override;

  CreateScheduledAiTaskTool(const CreateScheduledAiTaskTool&) = delete;
  CreateScheduledAiTaskTool& operator=(const CreateScheduledAiTaskTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Lists all scheduled tasks with their next-run time and last-run status.
class ListScheduledAiTasksTool : public Tool {
 public:
  explicit ListScheduledAiTasksTool(content::BrowserContext* browser_context);
  ~ListScheduledAiTasksTool() override;

  ListScheduledAiTasksTool(const ListScheduledAiTasksTool&) = delete;
  ListScheduledAiTasksTool& operator=(const ListScheduledAiTasksTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Edits an existing scheduled task by id. Any field omitted from the input
// keeps its current value - only the fields provided are changed.
class UpdateScheduledAiTaskTool : public Tool {
 public:
  explicit UpdateScheduledAiTaskTool(content::BrowserContext* browser_context);
  ~UpdateScheduledAiTaskTool() override;

  UpdateScheduledAiTaskTool(const UpdateScheduledAiTaskTool&) = delete;
  UpdateScheduledAiTaskTool& operator=(const UpdateScheduledAiTaskTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

// Deletes a scheduled task by id.
class DeleteScheduledAiTaskTool : public Tool {
 public:
  explicit DeleteScheduledAiTaskTool(content::BrowserContext* browser_context);
  ~DeleteScheduledAiTaskTool() override;

  DeleteScheduledAiTaskTool(const DeleteScheduledAiTaskTool&) = delete;
  DeleteScheduledAiTaskTool& operator=(const DeleteScheduledAiTaskTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_SCHEDULED_TASK_TOOLS_H_

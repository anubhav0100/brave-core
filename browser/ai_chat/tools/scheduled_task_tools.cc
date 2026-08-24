// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/scheduled_task_tools.h"

#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "brave/browser/ai_chat/ai_chat_service_factory.h"
#include "brave/components/ai_chat/core/browser/ai_chat_service.h"
#include "brave/components/ai_chat/core/browser/scheduled_tasks/scheduled_task_service.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameId[] = "id";
constexpr char kPropertyNameName[] = "name";
constexpr char kPropertyNamePrompt[] = "prompt";
constexpr char kPropertyNameRecurrence[] = "recurrence";
constexpr char kPropertyNameHour[] = "hour";
constexpr char kPropertyNameMinute[] = "minute";
constexpr char kPropertyNameWeekdays[] = "weekdays";
constexpr char kPropertyNameOneTimeDate[] = "one_time_date";
constexpr char kPropertyNameToolAllowlist[] = "tool_allowlist";
constexpr char kPropertyNameEnabled[] = "enabled";

constexpr char kNoServiceError[] =
    "Error: scheduled tasks aren't available in this profile.";

ScheduledTaskService* GetScheduledTaskService(
    content::BrowserContext* browser_context) {
  auto* ai_chat_service =
      AIChatServiceFactory::GetForBrowserContext(browser_context);
  return ai_chat_service ? ai_chat_service->GetScheduledTaskService()
                        : nullptr;
}

const char* WeekdayName(int day_of_week) {
  switch (day_of_week) {
    case 0:
      return "sunday";
    case 1:
      return "monday";
    case 2:
      return "tuesday";
    case 3:
      return "wednesday";
    case 4:
      return "thursday";
    case 5:
      return "friday";
    case 6:
      return "saturday";
    default:
      return "?";
  }
}

std::optional<int> WeekdayFromName(const std::string& name) {
  for (int i = 0; i < 7; ++i) {
    if (base::EqualsCaseInsensitiveASCII(name, WeekdayName(i))) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<ScheduledTaskRecurrence> RecurrenceFromString(
    const std::string& value) {
  if (base::EqualsCaseInsensitiveASCII(value, "once")) {
    return ScheduledTaskRecurrence::kOnce;
  }
  if (base::EqualsCaseInsensitiveASCII(value, "daily")) {
    return ScheduledTaskRecurrence::kDaily;
  }
  if (base::EqualsCaseInsensitiveASCII(value, "weekly")) {
    return ScheduledTaskRecurrence::kWeekly;
  }
  return std::nullopt;
}

std::string RecurrenceToString(ScheduledTaskRecurrence recurrence) {
  switch (recurrence) {
    case ScheduledTaskRecurrence::kOnce:
      return "once";
    case ScheduledTaskRecurrence::kDaily:
      return "daily";
    case ScheduledTaskRecurrence::kWeekly:
      return "weekly";
  }
  return "once";
}

std::string RunStatusToString(ScheduledTaskRunStatus status) {
  switch (status) {
    case ScheduledTaskRunStatus::kNeverRun:
      return "never run";
    case ScheduledTaskRunStatus::kSuccess:
      return "success";
    case ScheduledTaskRunStatus::kFailed:
      return "failed";
    case ScheduledTaskRunStatus::kPartial:
      return "partial (stopped early)";
  }
  return "unknown";
}

std::string FormatTime(base::Time time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d %02d:%02d", exploded.year,
                            exploded.month, exploded.day_of_month,
                            exploded.hour, exploded.minute);
}

std::string FormatTaskSummary(const ScheduledTask& task) {
  std::string text = base::StrCat({"- ", task.name, " (id: ", task.id, ")\n",
                                   "  Enabled: ",
                                   task.enabled ? "yes" : "no", "\n",
                                   "  Recurrence: ",
                                   RecurrenceToString(task.recurrence), " at ",
                                   base::NumberToString(task.hour), ":",
                                   task.minute < 10 ? "0" : "",
                                   base::NumberToString(task.minute), "\n"});
  if (task.recurrence == ScheduledTaskRecurrence::kWeekly &&
      !task.weekdays.empty()) {
    std::string weekdays;
    for (int day : task.weekdays) {
      if (!weekdays.empty()) {
        base::StrAppend(&weekdays, {", "});
      }
      base::StrAppend(&weekdays, {WeekdayName(day)});
    }
    base::StrAppend(&text, {"  Weekdays: ", weekdays, "\n"});
  }
  base::StrAppend(&text, {"  Next run: ",
                          task.next_run_time
                              ? FormatTime(*task.next_run_time)
                              : "(disabled)",
                          "\n"});
  base::StrAppend(&text, {"  Tool allowlist: ",
                          task.tool_allowlist.empty()
                              ? "(none - prompt can't use any tools)"
                              : base::JoinString(
                                    std::vector<std::string>(
                                        task.tool_allowlist.begin(),
                                        task.tool_allowlist.end()),
                                    ", "),
                          "\n"});
  base::StrAppend(&text,
                  {"  Last run: ",
                   task.last_run_time ? FormatTime(*task.last_run_time)
                                      : "never",
                   task.last_run_time
                       ? base::StrCat({" (", RunStatusToString(
                                                 task.last_run_status),
                                       ")"})
                       : "",
                   "\n"});
  if (!task.last_run_summary.empty()) {
    base::StrAppend(&text, {"  Last run summary: ", task.last_run_summary,
                            "\n"});
  }
  return text;
}

base::DictValue BuildCreateOrUpdateProperties(bool for_update) {
  std::vector<std::pair<const std::string, base::DictValue>> properties;
  if (for_update) {
    properties.emplace_back(kPropertyNameId,
                            StringProperty("The task's id, from "
                                          "list_scheduled_ai_tasks."));
  }
  properties.emplace_back(
      kPropertyNameName, StringProperty("A short human-readable name for "
                                        "this task."));
  properties.emplace_back(
      kPropertyNamePrompt,
      StringProperty("The exact text to submit as a new AI Chat "
                    "conversation when this task fires."));
  properties.emplace_back(
      kPropertyNameRecurrence,
      StringProperty("How often this task runs.",
                    std::vector<std::string>{"once", "daily", "weekly"}));
  properties.emplace_back(
      kPropertyNameHour,
      IntegerProperty("Hour of the day to run at, 0-23, local time."));
  properties.emplace_back(
      kPropertyNameMinute,
      IntegerProperty("Minute of the hour to run at, 0-59."));
  properties.emplace_back(
      kPropertyNameWeekdays,
      ArrayProperty(
          "Only used when recurrence is \"weekly\": which weekdays to run "
          "on (monday, tuesday, wednesday, thursday, friday, saturday, "
          "sunday).",
          StringProperty("A weekday name.")));
  properties.emplace_back(
      kPropertyNameOneTimeDate,
      StringProperty("Only used when recurrence is \"once\": the calendar "
                    "date to run on, as YYYY-MM-DD."));
  properties.emplace_back(
      kPropertyNameToolAllowlist,
      ArrayProperty(
          "Exact tool names (the same names you normally call, e.g. "
          "run_n8n_workflow) this task is allowed to use without a person "
          "present to approve it. Any tool call outside this list is "
          "refused automatically when the task runs unattended - leave "
          "empty if the prompt doesn't need any tools.",
          StringProperty("A tool name.")));
  if (for_update) {
    properties.emplace_back(kPropertyNameEnabled,
                            BooleanProperty("Whether the task is active."));
  }
  base::DictValue dict;
  for (auto& [key, value] : properties) {
    dict.Set(key, std::move(value));
  }
  return dict;
}

// Applies whichever fields are present in `input` onto `task`. Returns an
// error string on a malformed value, or nullopt on success.
std::optional<std::string> ApplyTaskFieldsFromInput(const base::DictValue& input,
                                                    ScheduledTask& task) {
  if (const std::string* name = input.FindString(kPropertyNameName)) {
    task.name = *name;
  }
  if (const std::string* prompt = input.FindString(kPropertyNamePrompt)) {
    task.prompt = *prompt;
  }
  if (const std::string* recurrence =
          input.FindString(kPropertyNameRecurrence)) {
    auto parsed = RecurrenceFromString(*recurrence);
    if (!parsed) {
      return "Error: 'recurrence' must be one of once, daily, weekly.";
    }
    task.recurrence = *parsed;
  }
  if (auto hour = input.FindInt(kPropertyNameHour)) {
    if (*hour < 0 || *hour > 23) {
      return "Error: 'hour' must be between 0 and 23.";
    }
    task.hour = *hour;
  }
  if (auto minute = input.FindInt(kPropertyNameMinute)) {
    if (*minute < 0 || *minute > 59) {
      return "Error: 'minute' must be between 0 and 59.";
    }
    task.minute = *minute;
  }
  if (const base::ListValue* weekdays =
          input.FindList(kPropertyNameWeekdays)) {
    std::set<int> parsed_weekdays;
    for (const auto& value : *weekdays) {
      if (!value.is_string()) {
        continue;
      }
      auto day = WeekdayFromName(value.GetString());
      if (!day) {
        return base::StrCat(
            {"Error: unrecognized weekday '", value.GetString(), "'."});
      }
      parsed_weekdays.insert(*day);
    }
    task.weekdays = std::move(parsed_weekdays);
  }
  if (const std::string* one_time_date =
          input.FindString(kPropertyNameOneTimeDate)) {
    base::Time parsed;
    if (!base::Time::FromString(one_time_date->c_str(), &parsed)) {
      return "Error: 'one_time_date' must be a valid date, e.g. "
            "2026-09-01.";
    }
    task.one_time_date = parsed;
  }
  if (const base::ListValue* allowlist =
          input.FindList(kPropertyNameToolAllowlist)) {
    std::set<std::string> parsed_allowlist;
    for (const auto& value : *allowlist) {
      if (value.is_string()) {
        parsed_allowlist.insert(value.GetString());
      }
    }
    task.tool_allowlist = std::move(parsed_allowlist);
  }
  if (auto enabled = input.FindBool(kPropertyNameEnabled)) {
    task.enabled = *enabled;
  }
  if (task.recurrence == ScheduledTaskRecurrence::kOnce &&
      !task.one_time_date) {
    return "Error: recurrence 'once' requires 'one_time_date'.";
  }
  return std::nullopt;
}

}  // namespace

// CreateScheduledAiTaskTool ---------------------------------------------

CreateScheduledAiTaskTool::CreateScheduledAiTaskTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreateScheduledAiTaskTool::~CreateScheduledAiTaskTool() = default;

std::string_view CreateScheduledAiTaskTool::Name() const {
  return "create_scheduled_ai_task";
}

std::string_view CreateScheduledAiTaskTool::Description() const {
  return "Schedules a prompt to run automatically, completely unattended, "
        "at a time the user chooses (once, daily, or on chosen weekdays). "
        "At that time the browser submits the prompt as a brand new AI "
        "Chat conversation on its own - no person needs to be present. "
        "Since nobody is there to approve anything, the task may only use "
        "tools named in tool_allowlist; any other tool call it tries is "
        "refused automatically. Use list_scheduled_ai_tasks to see what's "
        "already scheduled.";
}

std::optional<base::DictValue> CreateScheduledAiTaskTool::InputProperties()
    const {
  return BuildCreateOrUpdateProperties(/*for_update=*/false);
}

std::optional<std::vector<std::string>>
CreateScheduledAiTaskTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameName, kPropertyNamePrompt,
                                  kPropertyNameRecurrence, kPropertyNameHour,
                                  kPropertyNameMinute};
}

void CreateScheduledAiTaskTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  auto* service = GetScheduledTaskService(browser_context_);
  if (!service) {
    std::move(callback).Run(CreateContentBlocksForText(kNoServiceError), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  ScheduledTask task;
  auto error = ApplyTaskFieldsFromInput(*input, task);
  if (error) {
    std::move(callback).Run(CreateContentBlocksForText(*error), {});
    return;
  }
  auto id = service->CreateTask(std::move(task));
  if (!id) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: 'name' and 'prompt' are required and can't be empty."),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Created scheduled task with id: ", *id})),
      {});
}

// ListScheduledAiTasksTool -----------------------------------------------

ListScheduledAiTasksTool::ListScheduledAiTasksTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

ListScheduledAiTasksTool::~ListScheduledAiTasksTool() = default;

std::string_view ListScheduledAiTasksTool::Name() const {
  return "list_scheduled_ai_tasks";
}

std::string_view ListScheduledAiTasksTool::Description() const {
  return "Lists every scheduled task (see create_scheduled_ai_task), with "
        "its recurrence, next run time, tool allowlist, and last-run "
        "status.";
}

void ListScheduledAiTasksTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  auto* service = GetScheduledTaskService(browser_context_);
  if (!service) {
    std::move(callback).Run(CreateContentBlocksForText(kNoServiceError), {});
    return;
  }
  auto tasks = service->GetTasks();
  if (tasks.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("No scheduled tasks yet."), {});
    return;
  }
  std::string text;
  for (const auto& task : tasks) {
    base::StrAppend(&text, {FormatTaskSummary(task)});
  }
  std::move(callback).Run(CreateContentBlocksForText(text), {});
}

// UpdateScheduledAiTaskTool -----------------------------------------------

UpdateScheduledAiTaskTool::UpdateScheduledAiTaskTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

UpdateScheduledAiTaskTool::~UpdateScheduledAiTaskTool() = default;

std::string_view UpdateScheduledAiTaskTool::Name() const {
  return "update_scheduled_ai_task";
}

std::string_view UpdateScheduledAiTaskTool::Description() const {
  return "Edits an existing scheduled task by id (from "
        "list_scheduled_ai_tasks) - e.g. to change its time, prompt, tool "
        "allowlist, or enable/disable it. Fields left out keep their "
        "current value.";
}

std::optional<base::DictValue> UpdateScheduledAiTaskTool::InputProperties()
    const {
  return BuildCreateOrUpdateProperties(/*for_update=*/true);
}

std::optional<std::vector<std::string>>
UpdateScheduledAiTaskTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameId};
}

void UpdateScheduledAiTaskTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  auto* service = GetScheduledTaskService(browser_context_);
  if (!service) {
    std::move(callback).Run(CreateContentBlocksForText(kNoServiceError), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* id =
      input.has_value() ? input->FindString(kPropertyNameId) : nullptr;
  if (!id || id->empty()) {
    std::move(callback).Run(CreateContentBlocksForText("Error: missing 'id'"),
                            {});
    return;
  }
  auto existing = service->GetTask(*id);
  if (!existing) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no scheduled task with that id. Call "
            "list_scheduled_ai_tasks to see valid ids."),
        {});
    return;
  }
  auto error = ApplyTaskFieldsFromInput(*input, *existing);
  if (error) {
    std::move(callback).Run(CreateContentBlocksForText(*error), {});
    return;
  }
  if (!service->UpdateTask(*id, *existing)) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to update the task."), {});
    return;
  }
  std::move(callback).Run(CreateContentBlocksForText("Updated the task."),
                          {});
}

// DeleteScheduledAiTaskTool -----------------------------------------------

DeleteScheduledAiTaskTool::DeleteScheduledAiTaskTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

DeleteScheduledAiTaskTool::~DeleteScheduledAiTaskTool() = default;

std::string_view DeleteScheduledAiTaskTool::Name() const {
  return "delete_scheduled_ai_task";
}

std::string_view DeleteScheduledAiTaskTool::Description() const {
  return "Deletes a scheduled task by id (from list_scheduled_ai_tasks).";
}

std::optional<base::DictValue> DeleteScheduledAiTaskTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameId, StringProperty("The task's id.")}});
}

std::optional<std::vector<std::string>>
DeleteScheduledAiTaskTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameId};
}

void DeleteScheduledAiTaskTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  auto* service = GetScheduledTaskService(browser_context_);
  if (!service) {
    std::move(callback).Run(CreateContentBlocksForText(kNoServiceError), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* id =
      input.has_value() ? input->FindString(kPropertyNameId) : nullptr;
  if (!id || id->empty()) {
    std::move(callback).Run(CreateContentBlocksForText("Error: missing 'id'"),
                            {});
    return;
  }
  if (!service->DeleteTask(*id)) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: no scheduled task with that id."),
        {});
    return;
  }
  std::move(callback).Run(CreateContentBlocksForText("Deleted the task."),
                          {});
}

}  // namespace ai_chat

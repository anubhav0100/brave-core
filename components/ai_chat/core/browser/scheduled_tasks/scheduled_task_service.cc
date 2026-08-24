// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/scheduled_tasks/scheduled_task_service.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/uuid.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/ai_chat_service.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace ai_chat {

namespace {

constexpr char kScheduledTasksPref[] = "brave.ai_chat.scheduled_tasks";

constexpr char kIdKey[] = "id";
constexpr char kNameKey[] = "name";
constexpr char kPromptKey[] = "prompt";
constexpr char kRecurrenceKey[] = "recurrence";
constexpr char kHourKey[] = "hour";
constexpr char kMinuteKey[] = "minute";
constexpr char kWeekdaysKey[] = "weekdays";
constexpr char kOneTimeDateKey[] = "one_time_date";
constexpr char kToolAllowlistKey[] = "tool_allowlist";
constexpr char kEnabledKey[] = "enabled";
constexpr char kLastRunTimeKey[] = "last_run_time";
constexpr char kLastRunStatusKey[] = "last_run_status";
constexpr char kLastRunSummaryKey[] = "last_run_summary";
constexpr char kLastConversationUuidKey[] = "last_conversation_uuid";
constexpr char kNextRunTimeKey[] = "next_run_time";

constexpr size_t kMaxSummaryLength = 500;
constexpr base::TimeDelta kSafetyTimeout = base::Minutes(10);

base::DictValue TaskToDict(const ScheduledTask& task) {
  base::DictValue dict;
  dict.Set(kIdKey, task.id);
  dict.Set(kNameKey, task.name);
  dict.Set(kPromptKey, task.prompt);
  dict.Set(kRecurrenceKey, static_cast<int>(task.recurrence));
  dict.Set(kHourKey, task.hour);
  dict.Set(kMinuteKey, task.minute);

  base::ListValue weekdays;
  for (int weekday : task.weekdays) {
    weekdays.Append(weekday);
  }
  dict.Set(kWeekdaysKey, std::move(weekdays));

  if (task.one_time_date) {
    dict.Set(kOneTimeDateKey, base::TimeToValue(*task.one_time_date));
  }

  base::ListValue allowlist;
  for (const auto& name : task.tool_allowlist) {
    allowlist.Append(name);
  }
  dict.Set(kToolAllowlistKey, std::move(allowlist));

  dict.Set(kEnabledKey, task.enabled);
  if (task.last_run_time) {
    dict.Set(kLastRunTimeKey, base::TimeToValue(*task.last_run_time));
  }
  dict.Set(kLastRunStatusKey, static_cast<int>(task.last_run_status));
  dict.Set(kLastRunSummaryKey, task.last_run_summary);
  dict.Set(kLastConversationUuidKey, task.last_conversation_uuid);
  if (task.next_run_time) {
    dict.Set(kNextRunTimeKey, base::TimeToValue(*task.next_run_time));
  }
  return dict;
}

ScheduledTask DictToTask(const base::DictValue& dict) {
  ScheduledTask task;
  if (const std::string* v = dict.FindString(kIdKey)) {
    task.id = *v;
  }
  if (const std::string* v = dict.FindString(kNameKey)) {
    task.name = *v;
  }
  if (const std::string* v = dict.FindString(kPromptKey)) {
    task.prompt = *v;
  }
  task.recurrence = static_cast<ScheduledTaskRecurrence>(
      dict.FindInt(kRecurrenceKey).value_or(0));
  task.hour = dict.FindInt(kHourKey).value_or(9);
  task.minute = dict.FindInt(kMinuteKey).value_or(0);

  if (const base::ListValue* list = dict.FindList(kWeekdaysKey)) {
    for (const auto& v : *list) {
      if (v.is_int()) {
        task.weekdays.insert(v.GetInt());
      }
    }
  }
  if (const base::Value* v = dict.Find(kOneTimeDateKey)) {
    task.one_time_date = base::ValueToTime(v);
  }
  if (const base::ListValue* list = dict.FindList(kToolAllowlistKey)) {
    for (const auto& v : *list) {
      if (v.is_string()) {
        task.tool_allowlist.insert(v.GetString());
      }
    }
  }
  task.enabled = dict.FindBool(kEnabledKey).value_or(true);
  if (const base::Value* v = dict.Find(kLastRunTimeKey)) {
    task.last_run_time = base::ValueToTime(v);
  }
  task.last_run_status = static_cast<ScheduledTaskRunStatus>(
      dict.FindInt(kLastRunStatusKey).value_or(0));
  if (const std::string* v = dict.FindString(kLastRunSummaryKey)) {
    task.last_run_summary = *v;
  }
  if (const std::string* v = dict.FindString(kLastConversationUuidKey)) {
    task.last_conversation_uuid = *v;
  }
  if (const base::Value* v = dict.Find(kNextRunTimeKey)) {
    task.next_run_time = base::ValueToTime(v);
  }
  return task;
}

std::string FormatRunTitle(const std::string& task_name, base::Time when) {
  base::Time::Exploded exploded;
  when.LocalExplode(&exploded);
  return base::StringPrintf("Scheduled: %s (%04d-%02d-%02d %02d:%02d)",
                            task_name.c_str(), exploded.year, exploded.month,
                            exploded.day_of_month, exploded.hour,
                            exploded.minute);
}

}  // namespace

ScheduledTask::ScheduledTask() = default;
ScheduledTask::ScheduledTask(const ScheduledTask&) = default;
ScheduledTask& ScheduledTask::operator=(const ScheduledTask&) = default;
ScheduledTask::~ScheduledTask() = default;

ScheduledTaskService::ActiveRun::ActiveRun() = default;
ScheduledTaskService::ActiveRun::~ActiveRun() = default;

ScheduledTaskService::ScheduledTaskService(AIChatService* ai_chat_service,
                                           PrefService* prefs,
                                           TasksChangedCallback on_tasks_changed)
    : ai_chat_service_(ai_chat_service),
      prefs_(prefs),
      on_tasks_changed_(std::move(on_tasks_changed)) {
  poll_timer_.Start(FROM_HERE, base::Seconds(60),
                    base::BindRepeating(&ScheduledTaskService::OnPollTick,
                                        weak_ptr_factory_.GetWeakPtr()));
  // Catch up immediately rather than waiting up to 60s for the first tick -
  // covers tasks that came due while the browser was closed.
  OnPollTick();
}

ScheduledTaskService::~ScheduledTaskService() = default;

// static
void ScheduledTaskService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kScheduledTasksPref);
}

std::vector<ScheduledTask> ScheduledTaskService::GetTasks() const {
  std::vector<ScheduledTask> tasks;
  for (const auto& item : prefs_->GetList(kScheduledTasksPref)) {
    if (item.is_dict()) {
      tasks.push_back(DictToTask(item.GetDict()));
    }
  }
  return tasks;
}

std::optional<ScheduledTask> ScheduledTaskService::GetTask(
    const std::string& id) const {
  for (auto& task : GetTasks()) {
    if (task.id == id) {
      return task;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ScheduledTaskService::CreateTask(
    ScheduledTask task) {
  if (task.name.empty() || task.prompt.empty()) {
    return std::nullopt;
  }
  if (task.hour < 0 || task.hour > 23 || task.minute < 0 ||
      task.minute > 59) {
    return std::nullopt;
  }
  task.id =
      base::StrCat({"scheduled_task:",
                    base::Uuid::GenerateRandomV4().AsLowercaseString().substr(
                        0, 8)});
  task.last_run_status = ScheduledTaskRunStatus::kNeverRun;
  task.next_run_time =
      task.enabled ? std::make_optional(ComputeNextRunTime(
                         task, base::Time::Now()))
                   : std::nullopt;
  SaveTask(task);
  NotifyChanged();
  return task.id;
}

bool ScheduledTaskService::UpdateTask(const std::string& id,
                                      ScheduledTask task) {
  std::optional<ScheduledTask> existing = GetTask(id);
  if (!existing) {
    return false;
  }
  if (task.name.empty() || task.prompt.empty()) {
    return false;
  }
  if (task.hour < 0 || task.hour > 23 || task.minute < 0 ||
      task.minute > 59) {
    return false;
  }
  task.id = id;
  // Run history is only ever written by the service itself, not the edit
  // form - preserve it across an edit.
  task.last_run_time = existing->last_run_time;
  task.last_run_status = existing->last_run_status;
  task.last_run_summary = existing->last_run_summary;
  task.last_conversation_uuid = existing->last_conversation_uuid;
  task.next_run_time =
      task.enabled ? std::make_optional(ComputeNextRunTime(
                         task, base::Time::Now()))
                   : std::nullopt;
  SaveTask(task);
  NotifyChanged();
  return true;
}

bool ScheduledTaskService::DeleteTask(const std::string& id) {
  bool found = false;
  {
    ScopedListPrefUpdate update(prefs_, kScheduledTasksPref);
    auto it = std::ranges::find_if(*update, [&id](const base::Value& item) {
      const std::string* item_id =
          item.is_dict() ? item.GetDict().FindString(kIdKey) : nullptr;
      return item_id && *item_id == id;
    });
    if (it != update->end()) {
      update->erase(it);
      found = true;
    }
  }
  std::erase(pending_run_queue_, id);
  if (found) {
    NotifyChanged();
  }
  return found;
}

bool ScheduledTaskService::SetTaskEnabled(const std::string& id,
                                          bool enabled) {
  std::optional<ScheduledTask> task = GetTask(id);
  if (!task) {
    return false;
  }
  task->enabled = enabled;
  task->next_run_time =
      enabled ? std::make_optional(
                    ComputeNextRunTime(*task, base::Time::Now()))
              : std::nullopt;
  SaveTask(*task);
  NotifyChanged();
  return true;
}

void ScheduledTaskService::OnRequestInProgressChanged(
    ConversationHandler* handler,
    bool in_progress) {
  if (in_progress) {
    return;
  }
  if (!active_run_ || handler != active_run_->handler.get()) {
    return;
  }
  // is_request_in_progress_ also goes false transiently between engine
  // turns of a multi-step tool-use loop (see
  // AIChatService::CanUnloadConversation's comment) - GetToolUseTaskState()
  // stays kRunning throughout that whole loop and only resets to kNone once
  // the loop has genuinely finished, so this correctly ignores those
  // transient lulls.
  if (handler->GetToolUseTaskState() != mojom::TaskState::kNone) {
    return;
  }

  std::string summary;
  const auto& history = handler->GetConversationHistory();
  if (!history.empty() && history.back()) {
    summary = history.back()->text;
  }
  FinishActiveRun(ScheduledTaskRunStatus::kSuccess, summary,
                  handler->get_conversation_uuid());
}

void ScheduledTaskService::OnPollTick() {
  base::Time now = base::Time::Now();
  for (const auto& task : GetTasks()) {
    if (!task.enabled || !task.next_run_time.has_value()) {
      continue;
    }
    if (*task.next_run_time > now) {
      continue;
    }
    if (active_run_ && active_run_->task_id == task.id) {
      continue;
    }
    if (std::find(pending_run_queue_.begin(), pending_run_queue_.end(),
                 task.id) != pending_run_queue_.end()) {
      continue;
    }
    pending_run_queue_.push_back(task.id);
  }
  MaybeStartNextQueuedRun();
}

void ScheduledTaskService::MaybeStartNextQueuedRun() {
  if (active_run_ || pending_run_queue_.empty()) {
    return;
  }
  std::string next_task_id = pending_run_queue_.front();
  pending_run_queue_.pop_front();
  StartRun(next_task_id);
}

void ScheduledTaskService::StartRun(const std::string& task_id) {
  std::optional<ScheduledTask> task = GetTask(task_id);
  if (!task || !task->enabled) {
    MaybeStartNextQueuedRun();
    return;
  }

  if (!ai_chat_service_->HasUserOptedIn()) {
    task->last_run_time = base::Time::Now();
    task->last_run_status = ScheduledTaskRunStatus::kFailed;
    task->last_run_summary =
        "AI Chat hasn't been set up yet - open the AI Assistant and accept "
        "its terms before scheduling tasks.";
    task->next_run_time = ComputeNextRunTime(*task, base::Time::Now());
    SaveTask(*task);
    NotifyChanged();
    MaybeStartNextQueuedRun();
    return;
  }

  ConversationHandler* handler = ai_chat_service_->CreateConversation();
  ai_chat_service_->RenameConversation(
      handler->get_conversation_uuid(),
      FormatRunTitle(task->name, base::Time::Now()));

  handler->SetUnattendedToolAllowlist(task->tool_allowlist);
  handler->SetUnattendedTaskActive(true);

  active_run_ = std::make_unique<ActiveRun>();
  active_run_->task_id = task_id;
  active_run_->handler = handler->GetWeakPtr();
  active_run_->safety_timeout.Start(
      FROM_HERE, kSafetyTimeout,
      base::BindOnce(&ScheduledTaskService::OnSafetyTimeout,
                     weak_ptr_factory_.GetWeakPtr()));

  conversation_observation_.Reset();
  conversation_observation_.Observe(handler);

  handler->SubmitHumanConversationEntry(task->prompt, std::nullopt);
}

void ScheduledTaskService::OnSafetyTimeout() {
  if (!active_run_) {
    return;
  }
  std::string conversation_uuid;
  if (ConversationHandler* handler = active_run_->handler.get()) {
    conversation_uuid = handler->get_conversation_uuid();
    handler->StopTask();
  }
  FinishActiveRun(ScheduledTaskRunStatus::kPartial,
                  "This task exceeded the 10-minute unattended time limit "
                  "and was stopped.",
                  conversation_uuid);
}

void ScheduledTaskService::FinishActiveRun(
    ScheduledTaskRunStatus status,
    const std::string& summary,
    const std::string& conversation_uuid) {
  if (!active_run_) {
    return;
  }

  if (ConversationHandler* handler = active_run_->handler.get()) {
    handler->SetUnattendedTaskActive(false);
  }
  conversation_observation_.Reset();

  std::optional<ScheduledTask> task = GetTask(active_run_->task_id);
  active_run_.reset();

  if (task) {
    task->last_run_time = base::Time::Now();
    task->last_run_status = status;
    task->last_run_summary = summary.substr(0, kMaxSummaryLength);
    task->last_conversation_uuid = conversation_uuid;
    if (task->recurrence == ScheduledTaskRecurrence::kOnce) {
      task->enabled = false;
      task->next_run_time = std::nullopt;
    } else {
      task->next_run_time = ComputeNextRunTime(*task, base::Time::Now());
    }
    SaveTask(*task);
  }
  NotifyChanged();
  MaybeStartNextQueuedRun();
}

base::Time ScheduledTaskService::ComputeNextRunTime(const ScheduledTask& task,
                                                    base::Time after) const {
  if (task.recurrence == ScheduledTaskRecurrence::kOnce) {
    base::Time::Exploded exploded;
    task.one_time_date.value_or(after).LocalExplode(&exploded);
    exploded.hour = task.hour;
    exploded.minute = task.minute;
    exploded.second = 0;
    exploded.millisecond = 0;
    base::Time result;
    return base::Time::FromLocalExploded(exploded, &result) ? result : after;
  }

  // Daily/weekly: find the next hour:minute strictly after `after`. Uses
  // base::Days() to step between candidate days, which is precise except
  // possibly by an hour across a DST transition - an acceptable imprecision
  // for a feature with minute-granularity polling, not worth the added
  // complexity of exact calendar-day stepping here.
  base::Time::Exploded exploded;
  after.LocalExplode(&exploded);
  exploded.hour = task.hour;
  exploded.minute = task.minute;
  exploded.second = 0;
  exploded.millisecond = 0;
  base::Time candidate;
  if (!base::Time::FromLocalExploded(exploded, &candidate)) {
    return after + base::Days(1);
  }

  if (task.recurrence == ScheduledTaskRecurrence::kDaily ||
      task.weekdays.empty()) {
    if (candidate <= after) {
      candidate += base::Days(1);
    }
    return candidate;
  }

  // kWeekly with at least one weekday selected.
  for (int i = 0; i < 8; ++i) {
    base::Time::Exploded candidate_exploded;
    candidate.LocalExplode(&candidate_exploded);
    if (candidate > after &&
        task.weekdays.contains(candidate_exploded.day_of_week)) {
      return candidate;
    }
    candidate += base::Days(1);
  }
  return candidate;
}

void ScheduledTaskService::SaveTask(const ScheduledTask& task) {
  ScopedListPrefUpdate update(prefs_, kScheduledTasksPref);
  for (auto& item : *update) {
    if (item.is_dict()) {
      const std::string* item_id = item.GetDict().FindString(kIdKey);
      if (item_id && *item_id == task.id) {
        item = base::Value(TaskToDict(task));
        return;
      }
    }
  }
  update->Append(TaskToDict(task));
}

void ScheduledTaskService::NotifyChanged() {
  if (on_tasks_changed_) {
    on_tasks_changed_.Run();
  }
}

}  // namespace ai_chat

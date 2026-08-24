// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_SCHEDULED_TASKS_SCHEDULED_TASK_SERVICE_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_SCHEDULED_TASKS_SCHEDULED_TASK_SERVICE_H_

#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "brave/components/ai_chat/core/browser/conversation_handler.h"

class PrefRegistrySimple;
class PrefService;

namespace ai_chat {

class AIChatService;

enum class ScheduledTaskRecurrence {
  kOnce = 0,
  kDaily = 1,
  kWeekly = 2,
};

enum class ScheduledTaskRunStatus {
  kNeverRun = 0,
  kSuccess = 1,
  kFailed = 2,
  kPartial = 3,
};

// One user-defined "run this prompt unattended at a set time" task,
// persisted in prefs and driven by ScheduledTaskService.
struct ScheduledTask {
  ScheduledTask();
  ScheduledTask(const ScheduledTask&);
  ScheduledTask& operator=(const ScheduledTask&);
  ~ScheduledTask();

  std::string id;
  std::string name;
  std::string prompt;
  ScheduledTaskRecurrence recurrence = ScheduledTaskRecurrence::kOnce;
  int hour = 9;    // local time-of-day, 0-23
  int minute = 0;  // 0-59
  // For kWeekly: 0=Sunday .. 6=Saturday. Unused for kOnce/kDaily.
  std::set<int> weekdays;
  // For kOnce only: the calendar date to run on (hour/minute above supply
  // the time of day). Unused for kDaily/kWeekly.
  std::optional<base::Time> one_time_date;
  // Tool Name()s this task may call without a permission challenge - see
  // ConversationHandler::SetUnattendedToolAllowlist(). Any other tool the
  // model tries to call during this task's run is refused instead of ever
  // blocking on a challenge nobody will answer.
  std::set<std::string> tool_allowlist;
  bool enabled = true;

  std::optional<base::Time> last_run_time;
  ScheduledTaskRunStatus last_run_status = ScheduledTaskRunStatus::kNeverRun;
  std::string last_run_summary;
  std::string last_conversation_uuid;

  // Cached for display - recomputed after every run and every create/update.
  std::optional<base::Time> next_run_time;
};

// Owned directly by AIChatService (one per profile, same as AIChatService
// itself - not a separate KeyedService, since it only ever needs the
// AIChatService that owns it). Persists user-defined ScheduledTask records
// and fires them - creating a real, titled ("Scheduled: <name> - <time>")
// AI Chat conversation and submitting the task's prompt as a human turn -
// at each task's next_run_time, completely unattended.
//
// Unattended tool use safety is delegated to ConversationHandler: see
// ConversationHandler::SetUnattendedToolAllowlist() (tool calls outside the
// allowlist are refused immediately instead of ever creating a permission
// challenge nobody will answer) and SetUnattendedTaskActive() (pins the
// handler so AIChatService::CanUnloadConversation() won't destroy it
// mid-run just because no UI client is connected).
//
// Its poll timer starts as soon as this object is constructed, which is as
// soon as AIChatService itself is - i.e. whenever anything first touches AI
// Chat in this profile, not necessarily at browser startup (AIChatService
// is created lazily). OnPollTick() runs once immediately at construction,
// so any task that came due while the browser was closed still catches up
// promptly rather than waiting up to 60s for the first regular tick.
class ScheduledTaskService : public ConversationHandler::Observer {
 public:
  // Invoked after every create/update/delete/enable-toggle and after every
  // run completes - lets AIChatService push mojom::ServiceObserver's
  // OnScheduledTasksChanged() to connected UI clients without this class
  // needing to know anything about mojom.
  using TasksChangedCallback = base::RepeatingClosure;

  ScheduledTaskService(AIChatService* ai_chat_service,
                       PrefService* prefs,
                       TasksChangedCallback on_tasks_changed);
  ~ScheduledTaskService() override;

  ScheduledTaskService(const ScheduledTaskService&) = delete;
  ScheduledTaskService& operator=(const ScheduledTaskService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::vector<ScheduledTask> GetTasks() const;
  std::optional<ScheduledTask> GetTask(const std::string& id) const;

  // Returns the new task's generated id, or nullopt if `task` is invalid
  // (empty name/prompt, or hour/minute out of range).
  std::optional<std::string> CreateTask(ScheduledTask task);
  bool UpdateTask(const std::string& id, ScheduledTask task);
  bool DeleteTask(const std::string& id);
  bool SetTaskEnabled(const std::string& id, bool enabled);

  // ConversationHandler::Observer:
  void OnRequestInProgressChanged(ConversationHandler* handler,
                                  bool in_progress) override;

 private:
  struct ActiveRun {
    ActiveRun();
    ~ActiveRun();
    ActiveRun(const ActiveRun&) = delete;
    ActiveRun& operator=(const ActiveRun&) = delete;

    std::string task_id;
    base::WeakPtr<ConversationHandler> handler;
    base::OneShotTimer safety_timeout;
  };

  void OnPollTick();
  void MaybeStartNextQueuedRun();
  void StartRun(const std::string& task_id);
  void FinishActiveRun(ScheduledTaskRunStatus status,
                       const std::string& summary,
                       const std::string& conversation_uuid);
  void OnSafetyTimeout();

  base::Time ComputeNextRunTime(const ScheduledTask& task,
                                base::Time after) const;
  void SaveTask(const ScheduledTask& task);
  void NotifyChanged();

  raw_ptr<AIChatService> ai_chat_service_ = nullptr;
  raw_ptr<PrefService> prefs_ = nullptr;
  TasksChangedCallback on_tasks_changed_;

  base::RepeatingTimer poll_timer_;
  std::deque<std::string> pending_run_queue_;
  std::unique_ptr<ActiveRun> active_run_;

  base::ScopedObservation<ConversationHandler, ConversationHandler::Observer>
      conversation_observation_{this};

  base::WeakPtrFactory<ScheduledTaskService> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_SCHEDULED_TASKS_SCHEDULED_TASK_SERVICE_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/delegation_tools.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/delegation/delegation_process_manager.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

constexpr char kPropertyNameTaskId[] = "task_id";
constexpr char kPropertyNameComments[] = "comments";
constexpr char kPropertyNameBrief[] = "brief";

std::string FormatStatus(const base::DictValue& state) {
  const std::string* phase = state.FindString("phase");
  const std::string* team = state.FindString("selectedTeamId");
  std::string summary = base::StrCat(
      {"Phase: ", phase ? *phase : "unknown",
       " | Team: ", team ? *team : "unknown"});

  const base::ListValue* tasks = state.FindList("tasks");
  if (tasks) {
    int scheduled = 0, on_hold = 0, in_progress = 0, done = 0;
    std::string pending_review;
    for (const auto& task_value : *tasks) {
      if (!task_value.is_dict()) {
        continue;
      }
      const base::DictValue& task = task_value.GetDict();
      const std::string* status = task.FindString("status");
      if (!status) {
        continue;
      }
      if (*status == "scheduled") {
        ++scheduled;
      } else if (*status == "on_hold") {
        ++on_hold;
        bool needs_approval =
            task.FindBool("requiresUserApproval").value_or(false);
        if (needs_approval) {
          const std::string* id = task.FindString("id");
          const std::string* title = task.FindString("title");
          base::StrAppend(&pending_review,
                          {"\n  - [", id ? *id : "?", "] ",
                           title ? *title : "(untitled)"});
        }
      } else if (*status == "in_progress") {
        ++in_progress;
      } else if (*status == "done") {
        ++done;
      }
    }
    base::StrAppend(
        &summary,
        {"\nTasks: ", base::NumberToString(scheduled), " scheduled, ",
         base::NumberToString(in_progress), " in progress, ",
         base::NumberToString(on_hold), " on hold, ",
         base::NumberToString(done), " done"});
    if (!pending_review.empty()) {
      base::StrAppend(&summary,
                      {"\nAwaiting your review (use "
                       "approve_delegation_task/reject_delegation_task "
                       "with the task id):",
                       pending_review});
    }
  }

  if (const base::DictValue* agent_statuses =
          state.FindDict("agentStatuses")) {
    std::string agents;
    for (const auto [key, value] : *agent_statuses) {
      base::StrAppend(&agents,
                      {"\n  - agent ", key, ": ",
                       value.is_string() ? value.GetString() : "unknown"});
    }
    if (!agents.empty()) {
      base::StrAppend(&summary, {"\nAgent statuses:", agents});
    }
  }

  if (std::optional<double> cost = state.FindDouble("totalEstimatedCost")) {
    base::StrAppend(&summary,
                    {"\nEstimated cost so far: $",
                     base::NumberToString(*cost)});
  }

  return summary;
}

}  // namespace

// OpenDelegationTool ------------------------------------------------------

OpenDelegationTool::OpenDelegationTool(DelegationProcessManager* manager,
                                       content::BrowserContext* browser_context)
    : manager_(manager), browser_context_(browser_context) {}

OpenDelegationTool::~OpenDelegationTool() = default;

std::string_view OpenDelegationTool::Name() const {
  return "open_delegation";
}

std::string_view OpenDelegationTool::Description() const {
  return "Starts the local Delegation multi-agent 3D simulation instance if "
         "it isn't already running, and opens it in a new tab. Use this "
         "when the user wants to design or watch a multi-agent team work on "
         "a project visually. First run can take a few minutes (one-time "
         "setup); subsequent opens are fast.";
}

void OpenDelegationTool::UseTool(const std::string& input_json,
                                 UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Delegation isn't available."), {});
    return;
  }
  manager_->EnsureStarted(base::BindOnce(
      &OpenDelegationTool::OnStarted, weak_ptr_factory_.GetWeakPtr(),
      std::move(callback)));
}

void OpenDelegationTool::OnStarted(UseToolCallback callback, bool success) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: Delegation failed to start or set up. Check the "
            "\"Delegation\" section in AI Assistant Settings for details - "
            "the first run needs Git and Node.js on PATH to clone, "
            "install, and build it."),
        {});
    return;
  }
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
        CreateContentBlocksForText("Error: no open browser window to open "
                                   "a tab in."),
        {});
    return;
  }
  GURL url(base::StrCat({manager_->base_url(), "/the-delegation/"}));
  web_contents->OpenURL(
      {url, content::Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
       ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false},
      /*navigation_handle_callback=*/{});
  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat({"Opened Delegation at ",
                                               url.spec(), "."})),
      {});
}

// GetDelegationStatusTool -------------------------------------------------

GetDelegationStatusTool::GetDelegationStatusTool(
    DelegationProcessManager* manager)
    : manager_(manager) {}

GetDelegationStatusTool::~GetDelegationStatusTool() = default;

std::string_view GetDelegationStatusTool::Name() const {
  return "get_delegation_status";
}

std::string_view GetDelegationStatusTool::Description() const {
  return "Reads the live status of the Delegation multi-agent simulation: "
         "current phase, task counts, which tasks are awaiting your review, "
         "each agent's current status, and estimated cost so far. Does not "
         "start Delegation - returns an error if it isn't running.";
}

void GetDelegationStatusTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Delegation isn't available."), {});
    return;
  }
  manager_->GetState(base::BindOnce(&GetDelegationStatusTool::OnState,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    std::move(callback)));
}

void GetDelegationStatusTool::OnState(UseToolCallback callback,
                                      bool success,
                                      base::DictValue state) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Delegation isn't running. Use open_delegation to start it "
            "first."),
        {});
    return;
  }
  std::move(callback).Run(CreateContentBlocksForText(FormatStatus(state)),
                          {});
}

// ApproveDelegationTaskTool ------------------------------------------------

ApproveDelegationTaskTool::ApproveDelegationTaskTool(
    DelegationProcessManager* manager)
    : manager_(manager) {}

ApproveDelegationTaskTool::~ApproveDelegationTaskTool() = default;

std::string_view ApproveDelegationTaskTool::Name() const {
  return "approve_delegation_task";
}

std::string_view ApproveDelegationTaskTool::Description() const {
  return "Approves a Delegation task that's on hold awaiting human-in-the-"
         "loop review, letting the agent's draft output become final. Get "
         "the task id from get_delegation_status first.";
}

std::optional<base::DictValue> ApproveDelegationTaskTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameTaskId,
        StringProperty("The id of the task to approve, from "
                       "get_delegation_status's output.")}});
}

std::optional<std::vector<std::string>>
ApproveDelegationTaskTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameTaskId};
}

void ApproveDelegationTaskTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Delegation isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* task_id =
      input.has_value() ? input->FindString(kPropertyNameTaskId) : nullptr;
  if (!task_id || task_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: 'task_id' is required"), {});
    return;
  }
  base::DictValue payload;
  payload.Set("taskId", *task_id);
  manager_->SendControlAction(
      "approveTask", std::move(payload),
      base::BindOnce(&ApproveDelegationTaskTool::OnResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ApproveDelegationTaskTool::OnResult(UseToolCallback callback,
                                         bool success,
                                         std::string error) {
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Task approved."
                                        : base::StrCat({"Error: ", error})),
      {});
}

// RejectDelegationTaskTool -------------------------------------------------

RejectDelegationTaskTool::RejectDelegationTaskTool(
    DelegationProcessManager* manager)
    : manager_(manager) {}

RejectDelegationTaskTool::~RejectDelegationTaskTool() = default;

std::string_view RejectDelegationTaskTool::Name() const {
  return "reject_delegation_task";
}

std::string_view RejectDelegationTaskTool::Description() const {
  return "Rejects a Delegation task that's on hold awaiting human-in-the-"
         "loop review, sending it back to the agent with feedback comments "
         "for revision. Get the task id from get_delegation_status first.";
}

std::optional<base::DictValue> RejectDelegationTaskTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameTaskId,
        StringProperty("The id of the task to reject, from "
                       "get_delegation_status's output.")},
       {kPropertyNameComments,
        StringProperty("Feedback for the agent explaining what to change.")}});
}

std::optional<std::vector<std::string>>
RejectDelegationTaskTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameTaskId, kPropertyNameComments};
}

void RejectDelegationTaskTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Delegation isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* task_id =
      input.has_value() ? input->FindString(kPropertyNameTaskId) : nullptr;
  const std::string* comments =
      input.has_value() ? input->FindString(kPropertyNameComments) : nullptr;
  if (!task_id || task_id->empty() || !comments) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: 'task_id' and 'comments' are required"),
        {});
    return;
  }
  base::DictValue payload;
  payload.Set("taskId", *task_id);
  payload.Set("comments", *comments);
  manager_->SendControlAction(
      "rejectTask", std::move(payload),
      base::BindOnce(&RejectDelegationTaskTool::OnResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void RejectDelegationTaskTool::OnResult(UseToolCallback callback,
                                        bool success,
                                        std::string error) {
  std::move(callback).Run(
      CreateContentBlocksForText(success ? "Task rejected with feedback."
                                        : base::StrCat({"Error: ", error})),
      {});
}

// InjectDelegationBriefTool -------------------------------------------------

InjectDelegationBriefTool::InjectDelegationBriefTool(
    DelegationProcessManager* manager)
    : manager_(manager) {}

InjectDelegationBriefTool::~InjectDelegationBriefTool() = default;

std::string_view InjectDelegationBriefTool::Name() const {
  return "inject_delegation_brief";
}

std::string_view InjectDelegationBriefTool::Description() const {
  return "Starts a new project in Delegation with the given brief, handing "
         "it to the currently selected agent team. Only works while "
         "Delegation is idle (no project already running) - check with "
         "get_delegation_status first.";
}

std::optional<base::DictValue> InjectDelegationBriefTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameBrief,
        StringProperty("The project brief to hand to the agent team.")}});
}

std::optional<std::vector<std::string>>
InjectDelegationBriefTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameBrief};
}

void InjectDelegationBriefTool::UseTool(const std::string& input_json,
                                        UseToolCallback callback) {
  if (!manager_) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: Delegation isn't available."), {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* brief =
      input.has_value() ? input->FindString(kPropertyNameBrief) : nullptr;
  if (!brief || brief->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: 'brief' is required"), {});
    return;
  }
  base::DictValue payload;
  payload.Set("brief", *brief);
  manager_->SendControlAction(
      "injectBrief", std::move(payload),
      base::BindOnce(&InjectDelegationBriefTool::OnResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void InjectDelegationBriefTool::OnResult(UseToolCallback callback,
                                         bool success,
                                         std::string error) {
  std::move(callback).Run(
      CreateContentBlocksForText(
          success ? "Brief submitted - open_delegation to watch the team "
                    "work."
                 : base::StrCat({"Error: ", error})),
      {});
}

}  // namespace ai_chat

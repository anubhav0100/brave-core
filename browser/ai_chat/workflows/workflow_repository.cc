// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_repository.h"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

#include "base/strings/strcat.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace ai_chat {

namespace {
constexpr char kWorkflowsPref[] = "brave.ai_chat.workflows";
constexpr char kIdKey[] = "id";

std::vector<std::string> GetCallFlowTargets(const WorkflowDefinition& def) {
  std::vector<std::string> targets;
  for (const auto& step : def.steps) {
    if (step.type == WorkflowStepType::kCallFlow && !step.flow_id.empty()) {
      targets.push_back(step.flow_id);
    }
  }
  return targets;
}

}  // namespace

WorkflowRepository::SaveResult::SaveResult() = default;
WorkflowRepository::SaveResult::SaveResult(const SaveResult&) = default;
WorkflowRepository::SaveResult& WorkflowRepository::SaveResult::operator=(
    const SaveResult&) = default;
WorkflowRepository::SaveResult::~SaveResult() = default;

WorkflowRepository::WorkflowRepository(PrefService* prefs) : prefs_(prefs) {}

WorkflowRepository::~WorkflowRepository() = default;

// static
void WorkflowRepository::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kWorkflowsPref);
}

WorkflowRepository::SaveResult WorkflowRepository::SaveWorkflow(
    const base::DictValue& definition_json) {
  SaveResult result;
  WorkflowParseResult parse_result = ParseWorkflowDefinition(definition_json);
  if (!parse_result.ok()) {
    result.errors = std::move(parse_result.errors);
    return result;
  }
  const WorkflowDefinition& definition = *parse_result.definition;

  if (std::optional<std::string> cycle = FindCallFlowCycle(definition)) {
    result.errors.push_back(
        {"", "Saving this workflow would create a call_flow cycle: " +
                 *cycle});
    return result;
  }

  ScopedListPrefUpdate update(prefs_, kWorkflowsPref);
  bool replaced = false;
  for (auto& item : *update) {
    if (item.is_dict()) {
      const std::string* item_id = item.GetDict().FindString(kIdKey);
      if (item_id && *item_id == definition.id) {
        item = base::Value(definition.ToValue());
        replaced = true;
        break;
      }
    }
  }
  if (!replaced) {
    update->Append(definition.ToValue());
  }
  result.id = definition.id;
  return result;
}

std::optional<WorkflowDefinition> WorkflowRepository::GetWorkflow(
    const std::string& id) const {
  for (const auto& item : prefs_->GetList(kWorkflowsPref)) {
    if (!item.is_dict()) {
      continue;
    }
    const std::string* item_id = item.GetDict().FindString(kIdKey);
    if (item_id && *item_id == id) {
      WorkflowParseResult result = ParseWorkflowDefinition(item.GetDict());
      if (result.definition) {
        return result.definition;
      }
    }
  }
  return std::nullopt;
}

std::vector<WorkflowDefinition> WorkflowRepository::ListWorkflows() const {
  std::vector<WorkflowDefinition> workflows;
  for (const auto& item : prefs_->GetList(kWorkflowsPref)) {
    if (!item.is_dict()) {
      continue;
    }
    WorkflowParseResult result = ParseWorkflowDefinition(item.GetDict());
    if (result.definition) {
      workflows.push_back(std::move(*result.definition));
    }
  }
  return workflows;
}

bool WorkflowRepository::PublishWorkflow(const std::string& id) {
  ScopedListPrefUpdate update(prefs_, kWorkflowsPref);
  for (auto& item : *update) {
    if (!item.is_dict()) {
      continue;
    }
    const std::string* item_id = item.GetDict().FindString(kIdKey);
    if (item_id && *item_id == id) {
      item.GetDict().Set("status",
                         WorkflowStatusToString(WorkflowStatus::kPublished));
      return true;
    }
  }
  return false;
}

bool WorkflowRepository::DeleteWorkflow(const std::string& id) {
  ScopedListPrefUpdate update(prefs_, kWorkflowsPref);
  auto it = std::ranges::find_if(*update, [&id](const base::Value& item) {
    const std::string* item_id =
        item.is_dict() ? item.GetDict().FindString(kIdKey) : nullptr;
    return item_id && *item_id == id;
  });
  if (it == update->end()) {
    return false;
  }
  update->erase(it);
  return true;
}

std::vector<std::string> WorkflowRepository::GetDependencies(
    const std::string& id) const {
  std::optional<WorkflowDefinition> definition = GetWorkflow(id);
  if (!definition) {
    return {};
  }
  return GetCallFlowTargets(*definition);
}

std::vector<std::string> WorkflowRepository::GetDependents(
    const std::string& id) const {
  std::vector<std::string> dependents;
  for (const auto& other : ListWorkflows()) {
    if (other.id == id) {
      continue;
    }
    for (const auto& target : GetCallFlowTargets(other)) {
      if (target == id) {
        dependents.push_back(other.id);
        break;
      }
    }
  }
  return dependents;
}

std::optional<std::string> WorkflowRepository::FindCallFlowCycle(
    const WorkflowDefinition& new_definition) const {
  std::vector<std::string> path{new_definition.id};
  std::set<std::string> on_path{new_definition.id};

  std::function<std::optional<std::string>(const std::string&)> visit =
      [&](const std::string& id) -> std::optional<std::string> {
    std::vector<std::string> targets;
    if (id == new_definition.id) {
      targets = GetCallFlowTargets(new_definition);
    } else {
      std::optional<WorkflowDefinition> def = GetWorkflow(id);
      if (!def) {
        return std::nullopt;
      }
      targets = GetCallFlowTargets(*def);
    }

    for (const auto& target : targets) {
      if (on_path.contains(target)) {
        std::string description;
        for (const auto& step_id : path) {
          base::StrAppend(&description, {step_id, " -> "});
        }
        base::StrAppend(&description, {target});
        return description;
      }
      on_path.insert(target);
      path.push_back(target);
      if (std::optional<std::string> found = visit(target)) {
        return found;
      }
      path.pop_back();
      on_path.erase(target);
    }
    return std::nullopt;
  };

  return visit(new_definition.id);
}

}  // namespace ai_chat

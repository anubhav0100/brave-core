// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/workflows/workflow_repository.h"

#include <algorithm>
#include <utility>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace ai_chat {

namespace {
constexpr char kWorkflowsPref[] = "brave.ai_chat.workflows";
constexpr char kIdKey[] = "id";
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

}  // namespace ai_chat

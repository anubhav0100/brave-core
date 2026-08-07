// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_REPOSITORY_H_
#define BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_REPOSITORY_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/workflows/workflow_definition.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

namespace ai_chat {

// Local storage for workflow definitions - Phase 2 ("Workflow Repository")
// of the workflow orchestration engine design doc. Persists to a profile
// pref, mirroring the pattern already used for custom (BYOM) models and
// webhook tools in this codebase (a pref list of dicts, read/written
// wholesale rather than through a real database).
class WorkflowRepository : public KeyedService {
 public:
  explicit WorkflowRepository(PrefService* prefs);
  ~WorkflowRepository() override;

  WorkflowRepository(const WorkflowRepository&) = delete;
  WorkflowRepository& operator=(const WorkflowRepository&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  struct SaveResult {
    SaveResult();
    SaveResult(const SaveResult&);
    SaveResult& operator=(const SaveResult&);
    ~SaveResult();

    std::optional<std::string> id;  // Set only on success.
    std::vector<WorkflowValidationError> errors;
  };

  // Validates `definition_json` (see workflow_definition.h) and, if valid,
  // creates a new workflow or overwrites the existing one with the same id.
  SaveResult SaveWorkflow(const base::DictValue& definition_json);

  std::optional<WorkflowDefinition> GetWorkflow(const std::string& id) const;
  std::vector<WorkflowDefinition> ListWorkflows() const;

  // Returns false if no workflow with `id` exists.
  bool PublishWorkflow(const std::string& id);
  bool DeleteWorkflow(const std::string& id);

 private:
  raw_ptr<PrefService> prefs_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_REPOSITORY_H_

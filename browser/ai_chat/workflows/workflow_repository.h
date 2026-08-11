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
  // Also rejects it (with a validation error, not stored) if any of its
  // call_flow steps would create a reference cycle (directly or through
  // one or more other saved workflows) - Phase 4's "reject A->A, A->B->A"
  // requirement (see brave-ai-assistant-workflow-orchestration-engine.md
  // section 22, "Recursion Protection").
  SaveResult SaveWorkflow(const base::DictValue& definition_json);

  std::optional<WorkflowDefinition> GetWorkflow(const std::string& id) const;
  std::vector<WorkflowDefinition> ListWorkflows() const;

  // Returns false if no workflow with `id` exists.
  bool PublishWorkflow(const std::string& id);
  bool DeleteWorkflow(const std::string& id);

  // The ids of workflows `id`'s call_flow steps reference, and the ids of
  // other saved workflows whose call_flow steps reference `id` -
  // section 23's dependency graph, in its simplest useful form (no version
  // pinning, since GetWorkflow() itself doesn't support multiple stored
  // versions per id yet).
  std::vector<std::string> GetDependencies(const std::string& id) const;
  std::vector<std::string> GetDependents(const std::string& id) const;

 private:
  // Returns a human-readable "A -> B -> A" description of the first cycle
  // found reachable from `new_definition`'s own call_flow steps (treating
  // `new_definition` itself, not whatever's currently stored under its id,
  // as the starting point - it may not be saved yet), or nullopt if none.
  std::optional<std::string> FindCallFlowCycle(
      const WorkflowDefinition& new_definition) const;

  raw_ptr<PrefService> prefs_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_WORKFLOWS_WORKFLOW_REPOSITORY_H_

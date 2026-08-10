// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_DELEGATION_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_DELEGATION_TOOLS_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class DelegationProcessManager;

// Starts Delegation if needed and opens its editor UI in a new tab, same
// shape as OpenN8nTool.
class OpenDelegationTool : public Tool {
 public:
  OpenDelegationTool(DelegationProcessManager* manager,
                     content::BrowserContext* browser_context);
  ~OpenDelegationTool() override;

  OpenDelegationTool(const OpenDelegationTool&) = delete;
  OpenDelegationTool& operator=(const OpenDelegationTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnStarted(UseToolCallback callback, bool success);

  raw_ptr<DelegationProcessManager> manager_ = nullptr;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<OpenDelegationTool> weak_ptr_factory_{this};
};

// Reads live simulation state (phase, tasks, agent statuses, recent
// action log, token/cost usage) via DelegationProcessManager::GetState -
// does not start Delegation as a side effect (matches
// N8nProcessManager::ListMcpWorkflows's rule).
class GetDelegationStatusTool : public Tool {
 public:
  explicit GetDelegationStatusTool(DelegationProcessManager* manager);
  ~GetDelegationStatusTool() override;

  GetDelegationStatusTool(const GetDelegationStatusTool&) = delete;
  GetDelegationStatusTool& operator=(const GetDelegationStatusTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnState(UseToolCallback callback,
              bool success,
              base::DictValue state);

  raw_ptr<DelegationProcessManager> manager_ = nullptr;

  base::WeakPtrFactory<GetDelegationStatusTool> weak_ptr_factory_{this};
};

// Approves a task that's on hold pending human-in-the-loop review.
class ApproveDelegationTaskTool : public Tool {
 public:
  explicit ApproveDelegationTaskTool(DelegationProcessManager* manager);
  ~ApproveDelegationTaskTool() override;

  ApproveDelegationTaskTool(const ApproveDelegationTaskTool&) = delete;
  ApproveDelegationTaskTool& operator=(const ApproveDelegationTaskTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnResult(UseToolCallback callback, bool success, std::string error);

  raw_ptr<DelegationProcessManager> manager_ = nullptr;

  base::WeakPtrFactory<ApproveDelegationTaskTool> weak_ptr_factory_{this};
};

// Rejects a task that's on hold pending human-in-the-loop review, with
// feedback comments the agent will see and can act on.
class RejectDelegationTaskTool : public Tool {
 public:
  explicit RejectDelegationTaskTool(DelegationProcessManager* manager);
  ~RejectDelegationTaskTool() override;

  RejectDelegationTaskTool(const RejectDelegationTaskTool&) = delete;
  RejectDelegationTaskTool& operator=(const RejectDelegationTaskTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnResult(UseToolCallback callback, bool success, std::string error);

  raw_ptr<DelegationProcessManager> manager_ = nullptr;

  base::WeakPtrFactory<RejectDelegationTaskTool> weak_ptr_factory_{this};
};

// Starts a new project brief - only takes effect while Delegation is idle
// (no project currently running), mirroring what the UI itself allows.
class InjectDelegationBriefTool : public Tool {
 public:
  explicit InjectDelegationBriefTool(DelegationProcessManager* manager);
  ~InjectDelegationBriefTool() override;

  InjectDelegationBriefTool(const InjectDelegationBriefTool&) = delete;
  InjectDelegationBriefTool& operator=(const InjectDelegationBriefTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnResult(UseToolCallback callback, bool success, std::string error);

  raw_ptr<DelegationProcessManager> manager_ = nullptr;

  base::WeakPtrFactory<InjectDelegationBriefTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_DELEGATION_TOOLS_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/run_workflow_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "brave/browser/ai_chat/workflows/workflow_repository.h"
#include "brave/browser/ai_chat/workflows/workflow_repository_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace ai_chat {

namespace {
constexpr char kPropertyNameWorkflowId[] = "workflow_id";
constexpr char kPropertyNameInputs[] = "inputs";
}  // namespace

RunWorkflowTool::RunWorkflowTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

RunWorkflowTool::~RunWorkflowTool() = default;

std::string_view RunWorkflowTool::Name() const {
  return mojom::kRunWorkflowToolName;
}

std::string_view RunWorkflowTool::Description() const {
  return "Runs a workflow the user has saved in the \"Workflows\" section of "
         "AI Assistant settings, against the current active tab. Supports "
         "linear steps (navigate, click, type, wait, set a variable, branch "
         "on a condition, complete, fail), nested/reusable flows "
         "(call_flow, with cycle and call-depth protection), loops "
         "(for_each/while/until with break/continue and an iteration cap), "
         "and two bounded AI steps (ai.extract for structured data "
         "extraction against a fixed schema, ai.decide for a choice among a "
         "fixed set of named outcomes). Tool/webhook calls, approval "
         "pauses, and the open-ended ai.action node aren't implemented yet, "
         "and the workflow will fail clearly if it uses any of those. Pass "
         "'inputs' as an object matching the workflow's declared inputs.";
}

std::optional<base::DictValue> RunWorkflowTool::InputProperties() const {
  return CreateInputProperties(
      {{kPropertyNameWorkflowId,
        StringProperty("The id of the saved workflow to run.")},
       {kPropertyNameInputs,
        ObjectProperty(
            "Input values for the workflow, matching its declared inputs.",
            {})}});
}

std::optional<std::vector<std::string>> RunWorkflowTool::RequiredProperties()
    const {
  return std::vector<std::string>{kPropertyNameWorkflowId};
}

void RunWorkflowTool::UseTool(const std::string& input_json,
                              UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const std::string* workflow_id = input->FindString(kPropertyNameWorkflowId);
  if (!workflow_id || workflow_id->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'workflow_id'"),
        {});
    return;
  }

  auto* repository =
      WorkflowRepositoryFactory::GetForBrowserContext(browser_context_);
  if (!repository) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: workflows are not available in "
                                   "this profile."),
        {});
    return;
  }
  std::optional<WorkflowDefinition> definition =
      repository->GetWorkflow(*workflow_id);
  if (!definition) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: no workflow found with id '", *workflow_id,
                          "'"})),
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
        CreateContentBlocksForText("Error: no active tab to run the "
                                   "workflow against."),
        {});
    return;
  }

  base::DictValue inputs;
  if (const base::DictValue* inputs_dict = input->FindDict(kPropertyNameInputs)) {
    inputs = inputs_dict->Clone();
  }

  WorkflowRuntime::Start(
      std::move(*definition), std::move(inputs), web_contents, repository,
      base::BindOnce(&RunWorkflowTool::OnRunComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void RunWorkflowTool::OnRunComplete(UseToolCallback callback,
                                    WorkflowRuntime::ExecutionResult result) {
  std::string text;
  if (result.success) {
    base::StrAppend(&text, {"Workflow completed successfully after ",
                            base::NumberToString(
                                result.executed_step_ids.size()),
                            " step(s)."});
    if (!result.outputs.empty()) {
      text += " Outputs:";
      for (const auto& [name, value] : result.outputs) {
        base::StrAppend(&text, {" ", name, "=", value});
      }
    }
  } else {
    base::StrAppend(&text, {"Workflow failed after ",
                            base::NumberToString(
                                result.executed_step_ids.size()),
                            " step(s): ", result.error_message});
  }
  std::move(callback).Run(CreateContentBlocksForText(text), {});
}

}  // namespace ai_chat

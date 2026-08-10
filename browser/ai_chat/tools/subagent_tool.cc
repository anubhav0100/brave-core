// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/subagent_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "brave/browser/ai_chat/model_service_factory.h"
#include "brave/components/ai_chat/core/browser/model_service.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

namespace ai_chat {

namespace {

constexpr char kToolName[] = "delegate_to_subagent";
constexpr char kPropertyTask[] = "task";
constexpr char kPropertyModelKey[] = "model_key";

}  // namespace

DelegateToSubagentTool::RunState::RunState() = default;
DelegateToSubagentTool::RunState::~RunState() = default;

DelegateToSubagentTool::DelegateToSubagentTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context),
      model_service_(ModelServiceFactory::GetForBrowserContext(
          browser_context)),
      url_loader_factory_(browser_context->GetDefaultStoragePartition()
                              ->GetURLLoaderFactoryForBrowserProcess()) {}

DelegateToSubagentTool::~DelegateToSubagentTool() = default;

std::string_view DelegateToSubagentTool::Name() const {
  return kToolName;
}

std::string_view DelegateToSubagentTool::Description() const {
  return "Delegates a self-contained subtask to an independent sub-agent - "
         "a separate model call that works only from the task text you "
         "give it (it does not see the rest of this conversation) and has "
         "no tools of its own. Only the sub-agent's final answer is "
         "returned to you; its intermediate reasoning is not shown to the "
         "user. Use this to isolate a large, exploratory, or mechanical "
         "subtask (e.g. drafting a section, reformatting data, working "
         "through a multi-step calculation) so it doesn't clutter this "
         "conversation, or to route a subtask to a specific model. Because "
         "the sub-agent has no memory of this conversation, include every "
         "fact and instruction it needs directly in the task text.";
}

std::optional<base::DictValue> DelegateToSubagentTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyTask,
        StringProperty(
            "The subtask for the sub-agent to complete, written as a "
            "self-contained instruction including all context it needs "
            "(it cannot see this conversation).")},
       {kPropertyModelKey,
        StringProperty(
            "Optional: the key of a specific model to run the sub-agent "
            "with, if you want a different model than this conversation's. "
            "Omit to use the default model.")}});
}

std::optional<std::vector<std::string>>
DelegateToSubagentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyTask};
}

void DelegateToSubagentTool::UseTool(const std::string& input_json,
                                     UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }

  const std::string* task = input->FindString(kPropertyTask);
  if (!task || task->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: 'task' is required"), {});
    return;
  }

  if (!model_service_) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: model service is unavailable for the sub-agent"),
        {});
    return;
  }

  std::string model_key = model_service_->GetDefaultModelKey();
  if (const std::string* requested_model_key =
          input->FindString(kPropertyModelKey);
      requested_model_key && !requested_model_key->empty() &&
      model_service_->GetModel(*requested_model_key)) {
    model_key = *requested_model_key;
  }

  // Custom (BYOM) models are this fork's only real path, and
  // EngineConsumerOAIRemote never dereferences credential_manager - it's
  // only used by the Leo-hosted/private-inference path. nullptr is safe
  // here rather than standing up a throwaway AIChatCredentialManager.
  std::unique_ptr<EngineConsumer> engine = model_service_->GetEngineForModel(
      model_key, url_loader_factory_, /*credential_manager=*/nullptr);
  if (!engine) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: could not create a model engine for the sub-agent"),
        {});
    return;
  }

  auto state = std::make_shared<RunState>();
  state->callback = std::move(callback);
  state->engine = std::move(engine);

  EngineConsumer::ConversationHistory history;
  history.push_back(mojom::ConversationTurn::New(
      base::Uuid::GenerateRandomV4().AsLowercaseString(),
      mojom::CharacterType::HUMAN, mojom::ActionType::QUERY, *task,
      std::nullopt /* prompt */, std::nullopt /* selected_text */,
      std::nullopt /* events */, base::Time::Now(), std::nullopt /* edits */,
      std::nullopt /* uploaded_files */, nullptr /* skill */,
      false /* from_brave_search_SERP */, std::nullopt /* model_key */,
      nullptr /* near_verification_status */));

  EngineConsumer* engine_ptr = state->engine.get();
  engine_ptr->GenerateAssistantResponse(
      {} /* page_contents */, history, /*is_temporary_chat=*/true,
      {} /* tools - sub-agents have none of their own in this version */,
      std::nullopt /* preferred_tool_name */,
      {} /* conversation_capabilities */,
      base::BindRepeating(&DelegateToSubagentTool::OnDataReceived,
                          weak_ptr_factory_.GetWeakPtr(), state),
      base::BindOnce(&DelegateToSubagentTool::OnCompleted,
                     weak_ptr_factory_.GetWeakPtr(), state));
}

void DelegateToSubagentTool::OnDataReceived(
    std::shared_ptr<RunState> state,
    EngineConsumer::GenerationResultData result) {
  if (!result.event || !result.event->is_completion_event()) {
    return;
  }
  if (state->engine->SupportsDeltaTextResponses()) {
    state->completion_text =
        base::StrCat({state->completion_text,
                      result.event->get_completion_event()->completion});
  } else {
    state->completion_text = result.event->get_completion_event()->completion;
  }
}

void DelegateToSubagentTool::OnCompleted(
    std::shared_ptr<RunState> state,
    EngineConsumer::GenerationResult result) {
  if (!result.has_value()) {
    std::move(state->callback)
        .Run(CreateContentBlocksForText(
                 "Error: sub-agent failed to generate a response"),
             {});
    return;
  }

  std::string final_text = state->completion_text;
  if (final_text.empty() && result->event &&
      result->event->is_completion_event()) {
    final_text = result->event->get_completion_event()->completion;
  }
  if (final_text.empty()) {
    final_text = "(sub-agent returned no text)";
  }

  std::move(state->callback).Run(CreateContentBlocksForText(final_text), {});
}

}  // namespace ai_chat

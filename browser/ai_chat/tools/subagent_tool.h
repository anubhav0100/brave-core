// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_SUBAGENT_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_SUBAGENT_TOOL_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/engine/engine_consumer.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

class ModelService;

// A Tool that lets the primary assistant delegate a self-contained subtask to
// an independent "sub-agent" - a separate, one-off model call working from
// only the task text provided (not the rest of this conversation), whose own
// intermediate reasoning stays out of the main transcript; only its final
// answer comes back as this tool's result. This is a bounded, scoped-down
// form of multi-agent orchestration: the primary model acts as an
// orchestrator that can spin up any number of these one-shot workers, but
// workers in this version have no tool access of their own and cannot
// recursively delegate further - both to avoid re-deriving
// ConversationHandler's nontrivial tool-use streaming/merging logic outside
// of ConversationHandler, and to keep sub-agent runs cheap and bounded to a
// single model call.
//
// Useful for isolating large/exploratory/mechanical subtasks (e.g. "draft
// this section," "reformat this data," "work through this multi-step
// calculation") so they don't clutter the primary conversation, and for
// routing a subtask to a specifically-chosen model (e.g. a faster/cheaper one
// for a mechanical task) independent of whatever model the primary
// conversation is using.
class DelegateToSubagentTool : public Tool {
 public:
  explicit DelegateToSubagentTool(content::BrowserContext* browser_context);
  ~DelegateToSubagentTool() override;

  DelegateToSubagentTool(const DelegateToSubagentTool&) = delete;
  DelegateToSubagentTool& operator=(const DelegateToSubagentTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  // Shared between the repeating data-received callback and the one-shot
  // completed callback for a single UseTool() invocation, since the
  // completion text is streamed as deltas and must be accumulated across
  // multiple data-received calls before the final result is known.
  struct RunState {
    RunState();
    ~RunState();

    UseToolCallback callback;
    std::unique_ptr<EngineConsumer> engine;
    std::string completion_text;
  };

  void OnDataReceived(std::shared_ptr<RunState> state,
                      EngineConsumer::GenerationResultData result);
  void OnCompleted(std::shared_ptr<RunState> state,
                   EngineConsumer::GenerationResult result);

  raw_ptr<content::BrowserContext> browser_context_;
  raw_ptr<ModelService> model_service_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  base::WeakPtrFactory<DelegateToSubagentTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_SUBAGENT_TOOL_H_

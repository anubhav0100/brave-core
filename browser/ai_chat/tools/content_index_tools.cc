// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/content_index_tools.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyNameQuery[] = "query";
constexpr size_t kTopK = 5;
}  // namespace

SearchIndexedContentTool::SearchIndexedContentTool(AiChatContentIndex* index)
    : index_(index) {}

SearchIndexedContentTool::~SearchIndexedContentTool() = default;

std::string_view SearchIndexedContentTool::Name() const {
  return mojom::kSearchIndexedContentToolName;
}

std::string_view SearchIndexedContentTool::Description() const {
  return "Searches this profile's on-device content index - pages captured "
         "with capture_page_to_session and responses saved with "
         "save_response_to_memory, across all past conversations, not just "
         "this one - for chunks relevant to a query, using on-device "
         "embedding similarity rather than exact text matching. Call this "
         "to ground an answer in content the user actually captured/saved "
         "instead of relying only on what's visible in this conversation.";
}

std::optional<base::DictValue> SearchIndexedContentTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameQuery,
        StringProperty("What to search for.")}});
}

std::optional<std::vector<std::string>>
SearchIndexedContentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameQuery};
}

void SearchIndexedContentTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  if (!index_ || !index_->IsAvailable()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Content indexing isn't available right now - make sure it's "
            "enabled in Settings and the on-device model has finished "
            "loading."),
        {});
    return;
  }
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }
  const std::string* query = input->FindString(kPropertyNameQuery);
  if (!query || query->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'query'"), {});
    return;
  }
  index_->Search(*query, kTopK,
                base::BindOnce(&SearchIndexedContentTool::OnSearchComplete,
                               weak_ptr_factory_.GetWeakPtr(),
                               std::move(callback)));
}

void SearchIndexedContentTool::OnSearchComplete(
    UseToolCallback callback,
    std::vector<ContentSearchResult> results) {
  if (results.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("No relevant content found in the index."),
        {});
    return;
  }
  std::string text = "Found relevant content:\n\n";
  for (const auto& result : results) {
    base::StrAppend(
        &text, {"[", result.source_type, "] ", result.source_label,
               result.source_url.empty()
                   ? ""
                   : base::StrCat({" (", result.source_url, ")"}),
               "\n", result.text, "\n\n"});
  }
  std::move(callback).Run(CreateContentBlocksForText(text), {});
}

}  // namespace ai_chat

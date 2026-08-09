// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/content_index_tools.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "url/gurl.h"

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
  std::vector<mojom::WebSourcePtr> sources;
  for (const auto& result : results) {
    base::StrAppend(
        &text, {"[", result.source_type, "] ", result.source_label,
               result.source_url.empty()
                   ? ""
                   : base::StrCat({" (", result.source_url, ")"}),
               "\n", result.text, "\n\n"});

    // Only results with a real URL (captured pages, bookmarks - not saved
    // responses, which have none) become a citation the chat UI can link
    // to. Reuses the same WebSourcesContentBlock/SourcesEvent mechanism
    // server-side search tools already render "Sources" chips from - see
    // ConversationHandler::RespondToToolUseRequest.
    if (result.source_url.empty()) {
      continue;
    }
    GURL url(result.source_url);
    if (!url.is_valid()) {
      continue;
    }
    sources.push_back(mojom::WebSource::New(
        result.source_label, url, GURL(), result.text,
        std::vector<std::string>()));
  }

  std::vector<mojom::ContentBlockPtr> output;
  output.push_back(mojom::ContentBlock::NewTextContentBlock(
      mojom::TextContentBlock::New(text)));
  if (!sources.empty()) {
    output.push_back(mojom::ContentBlock::NewWebSourcesContentBlock(
        mojom::WebSourcesContentBlock::New(
            std::move(sources), std::vector<std::string>(),
            std::vector<std::string>())));
  }
  std::move(callback).Run(std::move(output), {});
}

// IndexBookmarksTool -------------------------------------------------------

namespace {

void CollectBookmarks(const bookmarks::BookmarkNode* node,
                      AiChatContentIndex* index,
                      int* count) {
  if (node->is_url()) {
    std::string title = base::UTF16ToUTF8(node->GetTitle());
    index->IndexChunks("bookmark", title.empty() ? node->url().spec() : title,
                       node->url().spec(), {title});
    ++*count;
    return;
  }
  for (const auto& child : node->children()) {
    CollectBookmarks(child.get(), index, count);
  }
}

}  // namespace

IndexBookmarksTool::IndexBookmarksTool(AiChatContentIndex* index,
                                       Profile* profile)
    : index_(index), profile_(profile) {}

IndexBookmarksTool::~IndexBookmarksTool() = default;

std::string_view IndexBookmarksTool::Name() const {
  return "index_bookmarks";
}

std::string_view IndexBookmarksTool::Description() const {
  return "Indexes the user's current bookmarks (title and URL) into this "
         "profile's on-device content index, so search_indexed_content can "
         "find them later. Call this once when the user asks you to "
         "search/remember their bookmarks and you haven't indexed them "
         "yet this session - it does not run automatically.";
}

void IndexBookmarksTool::UseTool(const std::string& input_json,
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
  auto* model = BookmarkModelFactory::GetForBrowserContext(profile_);
  if (!model || !model->loaded()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: bookmarks aren't available."), {});
    return;
  }
  int count = 0;
  CollectBookmarks(model->root_node(), index_, &count);
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Indexed ", base::NumberToString(count),
                        " bookmark(s)."})),
      {});
}

}  // namespace ai_chat

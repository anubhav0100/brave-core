// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_CONTENT_INDEX_TOOLS_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_CONTENT_INDEX_TOOLS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

class Profile;

namespace ai_chat {

class AiChatContentIndex;
struct ContentSearchResult;

// Searches this profile's on-device content index (pages captured with
// capture_page_to_session and responses saved with save_response_to_memory,
// across all conversations - not just this one) for chunks relevant to a
// query, using on-device embedding similarity rather than exact text
// matching. This is retrieval-augmented generation: call this to ground an
// answer in content the user has actually captured/saved, instead of
// relying only on what's visible in the current conversation. Returns a
// clear "not available" message (not an error) if the user hasn't enabled
// content indexing in Settings, or the on-device embedder isn't ready yet.
class SearchIndexedContentTool : public Tool {
 public:
  explicit SearchIndexedContentTool(AiChatContentIndex* index);
  ~SearchIndexedContentTool() override;

  SearchIndexedContentTool(const SearchIndexedContentTool&) = delete;
  SearchIndexedContentTool& operator=(const SearchIndexedContentTool&) =
      delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnSearchComplete(UseToolCallback callback,
                        std::vector<ContentSearchResult> results);

  raw_ptr<AiChatContentIndex> index_ = nullptr;

  base::WeakPtrFactory<SearchIndexedContentTool> weak_ptr_factory_{this};
};

// Indexes the user's current bookmarks (title + URL, one chunk each) into
// this profile's content index, so search_indexed_content can find them
// later ("what was that site I bookmarked about X"). Call this once to
// (re)sync - it doesn't run automatically on every bookmark change, since
// that would mean walking the whole bookmark tree on every add/remove.
class IndexBookmarksTool : public Tool {
 public:
  IndexBookmarksTool(AiChatContentIndex* index, Profile* profile);
  ~IndexBookmarksTool() override;

  IndexBookmarksTool(const IndexBookmarksTool&) = delete;
  IndexBookmarksTool& operator=(const IndexBookmarksTool&) = delete;

  std::string_view Name() const override;
  std::string_view Description() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  raw_ptr<AiChatContentIndex> index_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_CONTENT_INDEX_TOOLS_H_

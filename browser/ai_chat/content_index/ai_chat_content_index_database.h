// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_DATABASE_H_
#define BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_DATABASE_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "sql/database.h"

namespace ai_chat {

// One persisted chunk row, as loaded back from disk at startup.
struct StoredContentChunk {
  StoredContentChunk();
  StoredContentChunk(StoredContentChunk&&);
  StoredContentChunk& operator=(StoredContentChunk&&);
  ~StoredContentChunk();

  std::string source_type;
  std::string source_label;
  std::string source_url;
  std::string text;
  std::vector<float> embedding;
};

// On-disk store backing AiChatContentIndex, so indexed content survives
// browser restarts (see brave-ai-chat-rag-vector-memory-engine.md, "Phase
// 1.5" - Phase 1 was deliberately in-memory-only). Lives entirely on a
// background sequence - only ever constructed/called through
// base::SequenceBound<AiChatContentIndexDatabase>; see the owner
// (AiChatContentIndex) for how it's wired up.
//
// Unencrypted, unlike AIChatDatabase (conversation history): this stores
// the same page/response text that page capture and response memory
// already hold in cleartext session buffers upstream of it, and is closer
// in sensitivity to History/Bookmarks (also unencrypted SQLite) than to
// full conversation transcripts.
class AiChatContentIndexDatabase {
 public:
  explicit AiChatContentIndexDatabase(const base::FilePath& db_file_path);
  AiChatContentIndexDatabase(const AiChatContentIndexDatabase&) = delete;
  AiChatContentIndexDatabase& operator=(const AiChatContentIndexDatabase&) =
      delete;
  ~AiChatContentIndexDatabase();

  // Loads every persisted chunk, oldest first. Lazily opens/creates the
  // database on first call. Returns empty on any failure - a failed load
  // just means the index starts empty for this session, not a fatal error.
  std::vector<StoredContentChunk> LoadAllChunks();

  void AddChunk(const std::string& source_type,
               const std::string& source_label,
               const std::string& source_url,
               const std::string& text,
               const std::vector<float>& embedding);

  void Clear();

 private:
  bool LazyInit();
  bool CreateSchema();

  const base::FilePath db_file_path_;
  sql::Database db_ GUARDED_BY_CONTEXT(sequence_checker_);
  bool init_attempted_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  bool init_succeeded_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_DATABASE_H_

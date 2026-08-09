// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/content_index/ai_chat_content_index_database.h"

#include <tuple>
#include <utility>

#include "base/containers/span.h"
#include "sql/meta_table.h"
#include "sql/statement.h"

namespace ai_chat {

namespace {
constexpr int kCurrentDatabaseVersion = 1;
constexpr int kCompatibleDatabaseVersion = 1;
}  // namespace

StoredContentChunk::StoredContentChunk() = default;
StoredContentChunk::StoredContentChunk(StoredContentChunk&&) = default;
StoredContentChunk& StoredContentChunk::operator=(StoredContentChunk&&) =
    default;
StoredContentChunk::~StoredContentChunk() = default;

AiChatContentIndexDatabase::AiChatContentIndexDatabase(
    const base::FilePath& db_file_path)
    : db_file_path_(db_file_path),
      db_(sql::DatabaseOptions().set_page_size(4096).set_cache_size(500),
          sql::Database::Tag("AiChatContentIndex")) {
  // Constructed on the owner's sequence, used only on the background
  // sequence it's bound to from then on.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

AiChatContentIndexDatabase::~AiChatContentIndexDatabase() = default;

bool AiChatContentIndexDatabase::LazyInit() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (init_attempted_) {
    return init_succeeded_;
  }
  init_attempted_ = true;

  if (!db_.is_open() && !db_.Open(db_file_path_)) {
    return false;
  }

  if (sql::MetaTable::RazeIfIncompatible(&db_, kCompatibleDatabaseVersion,
                                         kCurrentDatabaseVersion) ==
      sql::RazeIfIncompatibleResult::kFailed) {
    return false;
  }

  sql::MetaTable meta_table;
  if (!meta_table.Init(&db_, kCurrentDatabaseVersion,
                       kCompatibleDatabaseVersion)) {
    return false;
  }

  init_succeeded_ = CreateSchema();
  return init_succeeded_;
}

bool AiChatContentIndexDatabase::CreateSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  static constexpr char kCreateTableQuery[] =
      "CREATE TABLE IF NOT EXISTS content_chunks("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "source_type TEXT NOT NULL,"
      "source_label TEXT NOT NULL,"
      "source_url TEXT NOT NULL,"
      "text TEXT NOT NULL,"
      "embedding BLOB NOT NULL)";
  return db_.Execute(kCreateTableQuery);
}

std::vector<StoredContentChunk> AiChatContentIndexDatabase::LoadAllChunks() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<StoredContentChunk> chunks;
  if (!LazyInit()) {
    return chunks;
  }
  static constexpr char kSelectQuery[] =
      "SELECT source_type, source_label, source_url, text, embedding "
      "FROM content_chunks ORDER BY id ASC";
  sql::Statement statement(db_.GetUniqueStatement(kSelectQuery));
  while (statement.Step()) {
    StoredContentChunk chunk;
    chunk.source_type = statement.ColumnString(0);
    chunk.source_label = statement.ColumnString(1);
    chunk.source_url = statement.ColumnString(2);
    chunk.text = statement.ColumnString(3);

    base::span<const uint8_t> blob = statement.ColumnBlob(4);
    chunk.embedding.resize(blob.size() / sizeof(float));
    if (!chunk.embedding.empty()) {
      base::as_writable_byte_span(base::allow_nonunique_obj, chunk.embedding)
          .copy_from(blob);
    }
    chunks.push_back(std::move(chunk));
  }
  return chunks;
}

void AiChatContentIndexDatabase::AddChunk(
    const std::string& source_type,
    const std::string& source_label,
    const std::string& source_url,
    const std::string& text,
    const std::vector<float>& embedding) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!LazyInit()) {
    return;
  }
  static constexpr char kInsertQuery[] =
      "INSERT INTO content_chunks"
      "(source_type, source_label, source_url, text, embedding) "
      "VALUES(?, ?, ?, ?, ?)";
  sql::Statement statement(db_.GetUniqueStatement(kInsertQuery));
  statement.BindString(0, source_type);
  statement.BindString(1, source_label);
  statement.BindString(2, source_url);
  statement.BindString(3, text);
  statement.BindBlob(4, base::as_byte_span(base::allow_nonunique_obj, embedding));
  statement.Run();
}

void AiChatContentIndexDatabase::Clear() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!LazyInit()) {
    return;
  }
  std::ignore = db_.Execute("DELETE FROM content_chunks");
}

}  // namespace ai_chat

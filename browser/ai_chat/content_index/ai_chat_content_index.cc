// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/content_index/ai_chat_content_index.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "base/functional/bind.h"
#include "brave/browser/history_embeddings/brave_passage_embeddings_service_controller.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace ai_chat {

namespace {
constexpr char kContentIndexingEnabledPref[] =
    "brave.ai_chat.content_indexing_enabled";
constexpr size_t kMaxChunkTextLength = 4000;
}  // namespace

IndexedChunk::IndexedChunk() = default;
IndexedChunk::IndexedChunk(IndexedChunk&&) = default;
IndexedChunk& IndexedChunk::operator=(IndexedChunk&&) = default;
IndexedChunk::~IndexedChunk() = default;

// static
void AiChatContentIndex::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kContentIndexingEnabledPref, false);
}

// static
bool AiChatContentIndex::IsEnabledForProfile(PrefService* prefs) {
  return prefs && prefs->GetBoolean(kContentIndexingEnabledPref);
}

AiChatContentIndex::AiChatContentIndex() {
  passage_embeddings::BravePassageEmbeddingsServiceController::Get()
      ->AddObserver(this);
}

AiChatContentIndex::~AiChatContentIndex() {
  passage_embeddings::BravePassageEmbeddingsServiceController::Get()
      ->RemoveObserver(this);
}

void AiChatContentIndex::EmbedderMetadataUpdated(
    passage_embeddings::EmbedderMetadata metadata) {
  embedder_metadata_ = metadata;
}

void AiChatContentIndex::IndexChunks(const std::string& source_type,
                                     const std::string& source_label,
                                     const std::string& source_url,
                                     const std::vector<std::string>& chunks) {
  if (!IsAvailable() || chunks.empty()) {
    return;
  }
  std::vector<std::string> passages;
  for (const auto& chunk : chunks) {
    passages.push_back(chunk.size() > kMaxChunkTextLength
                           ? chunk.substr(0, kMaxChunkTextLength)
                           : chunk);
  }
  auto* embedder =
      passage_embeddings::BravePassageEmbeddingsServiceController::Get()
          ->GetEmbedder();
  auto job = embedder->ComputePassagesEmbeddings(
      passage_embeddings::PassagePriority::kPassive, passages,
      base::BindOnce(&AiChatContentIndex::OnChunksEmbedded,
                     weak_ptr_factory_.GetWeakPtr(), source_type,
                     source_label, source_url));
  active_jobs_.insert(std::move(job));
}

void AiChatContentIndex::OnChunksEmbedded(
    std::string source_type,
    std::string source_label,
    std::string source_url,
    std::vector<std::string> passages,
    std::vector<passage_embeddings::Embedding> embeddings,
    uint64_t job_id,
    passage_embeddings::ComputeEmbeddingsStatus status) {
  active_jobs_.erase(job_id);
  if (status != passage_embeddings::ComputeEmbeddingsStatus::kSuccess) {
    return;
  }
  for (size_t i = 0; i < passages.size() && i < embeddings.size(); ++i) {
    IndexedChunk chunk;
    chunk.source_type = source_type;
    chunk.source_label = source_label;
    chunk.source_url = source_url;
    chunk.text = std::move(passages[i]);
    chunk.embedding = std::move(embeddings[i]);
    chunks_.push_back(std::move(chunk));
  }
}

void AiChatContentIndex::Search(const std::string& query,
                                size_t top_k,
                                SearchCallback callback) {
  if (!IsAvailable() || chunks_.empty() || query.empty()) {
    std::move(callback).Run({});
    return;
  }
  auto* embedder =
      passage_embeddings::BravePassageEmbeddingsServiceController::Get()
          ->GetEmbedder();
  auto job = embedder->ComputePassagesEmbeddings(
      passage_embeddings::PassagePriority::kUserInitiated, {query},
      base::BindOnce(&AiChatContentIndex::OnQueryEmbedded,
                     weak_ptr_factory_.GetWeakPtr(), top_k,
                     std::move(callback)));
  active_jobs_.insert(std::move(job));
}

void AiChatContentIndex::OnQueryEmbedded(
    size_t top_k,
    SearchCallback callback,
    std::vector<std::string> passages,
    std::vector<passage_embeddings::Embedding> embeddings,
    uint64_t job_id,
    passage_embeddings::ComputeEmbeddingsStatus status) {
  active_jobs_.erase(job_id);
  if (status != passage_embeddings::ComputeEmbeddingsStatus::kSuccess ||
      embeddings.empty()) {
    std::move(callback).Run({});
    return;
  }
  const passage_embeddings::Embedding& query_embedding = embeddings[0];

  std::vector<ContentSearchResult> results;
  results.reserve(chunks_.size());
  for (const auto& chunk : chunks_) {
    if (!chunk.embedding.has_value()) {
      continue;
    }
    ContentSearchResult result;
    result.source_type = chunk.source_type;
    result.source_label = chunk.source_label;
    result.source_url = chunk.source_url;
    result.text = chunk.text;
    result.score = query_embedding.ScoreWith(*chunk.embedding);
    results.push_back(std::move(result));
  }
  std::ranges::sort(results, std::ranges::greater{},
                    &ContentSearchResult::score);
  if (results.size() > top_k) {
    results.resize(top_k);
  }
  std::move(callback).Run(std::move(results));
}

void AiChatContentIndex::Clear() {
  chunks_.clear();
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_H_
#define BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "brave/browser/ai_chat/content_index/ai_chat_content_index_database.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/passage_embeddings/core/passage_embeddings_types.h"

class PrefRegistrySimple;
class PrefService;

namespace ai_chat {

// One chunk of content indexed for retrieval, with its embedding kept
// alongside it for similarity scoring at search time.
struct IndexedChunk {
  IndexedChunk();
  IndexedChunk(IndexedChunk&&);
  IndexedChunk& operator=(IndexedChunk&&);
  ~IndexedChunk();

  std::string source_type;   // "page" or "response".
  std::string source_label;  // Page heading, or response label.
  std::string source_url;    // Empty for "response" entries.
  std::string text;
  std::optional<passage_embeddings::Embedding> embedding;
};

struct ContentSearchResult {
  std::string source_type;
  std::string source_label;
  std::string source_url;
  std::string text;
  float score = 0;
};

// Profile-scoped vector index for AI Chat content - captured pages and
// saved responses. Embeds chunk text via
// passage_embeddings::BravePassageEmbeddingsServiceController's on-device
// model. Kept in memory for fast search, and mirrored to an on-disk SQLite
// store (AiChatContentIndexDatabase, on a background sequence) so content
// survives browser restarts - loaded back in asynchronously at
// construction time, see OnChunksLoadedFromDatabase(). Obtained via
// AiChatContentIndexFactory, not constructed directly - see
// brave-ai-chat-rag-vector-memory-engine.md for the full design.
class AiChatContentIndex : public KeyedService,
                           public passage_embeddings::EmbedderMetadataObserver {
 public:
  using SearchCallback =
      base::OnceCallback<void(std::vector<ContentSearchResult>)>;

  explicit AiChatContentIndex(const base::FilePath& profile_path);
  ~AiChatContentIndex() override;

  AiChatContentIndex(const AiChatContentIndex&) = delete;
  AiChatContentIndex& operator=(const AiChatContentIndex&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Whether the user has turned indexing on in Settings.
  static bool IsEnabledForProfile(PrefService* prefs);

  // Embeds and stores each of `chunks` under one `source_label`/
  // `source_url`. Silent no-op (not an error) if the on-device embedder
  // isn't available yet - see IsAvailable(). Callers (PageCaptureSession,
  // ResponseMemorySession) should check IsEnabledForProfile() themselves
  // before calling, since this class has no pref access of its own.
  void IndexChunks(const std::string& source_type,
                   const std::string& source_label,
                   const std::string& source_url,
                   const std::vector<std::string>& chunks);

  // Embeds `query` and returns the top `top_k` most similar stored chunks,
  // highest score first. Empty if nothing is indexed or the embedder isn't
  // available.
  void Search(const std::string& query, size_t top_k, SearchCallback callback);

  void Clear();
  size_t entry_count() const { return chunks_.size(); }

  // True once the on-device embedder has reported ready metadata.
  bool IsAvailable() const { return embedder_metadata_.has_value(); }

 private:
  // passage_embeddings::EmbedderMetadataObserver:
  void EmbedderMetadataUpdated(
      passage_embeddings::EmbedderMetadata metadata) override;

  void OnChunksEmbedded(std::string source_type,
                        std::string source_label,
                        std::string source_url,
                        std::vector<std::string> passages,
                        std::vector<passage_embeddings::Embedding> embeddings,
                        uint64_t job_id,
                        passage_embeddings::ComputeEmbeddingsStatus status);
  void OnQueryEmbedded(size_t top_k,
                      SearchCallback callback,
                      std::vector<std::string> passages,
                      std::vector<passage_embeddings::Embedding> embeddings,
                      uint64_t job_id,
                      passage_embeddings::ComputeEmbeddingsStatus status);

  // Rehydrates `chunks_` from disk once the background load issued at
  // construction time completes. Chunks indexed before this fires (a
  // capture/save that happens very early in startup) are appended after,
  // not lost - IndexChunks() doesn't wait on this.
  void OnChunksLoadedFromDatabase(std::vector<StoredContentChunk> chunks);

  std::optional<passage_embeddings::EmbedderMetadata> embedder_metadata_;
  std::vector<IndexedChunk> chunks_;

  // On-disk mirror of `chunks_`, lives on a background sequence. See
  // AiChatContentIndexDatabase for why this is unencrypted.
  base::SequenceBound<AiChatContentIndexDatabase> database_;

  // In-flight embedder jobs, keyed by id so a completed one can be erased -
  // Job is move-only RAII that cancels on destruction if not yet complete,
  // so these must be kept alive until their callback fires.
  base::flat_set<passage_embeddings::Embedder::Job,
                 passage_embeddings::Embedder::JobIdComparator>
      active_jobs_;

  base::WeakPtrFactory<AiChatContentIndex> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_CONTENT_INDEX_AI_CHAT_CONTENT_INDEX_H_

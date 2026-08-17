/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_MODEL_FETCHER_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_MODEL_FETCHER_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"

namespace ai_chat {

class ModelService;

// Manages syncing the model(s) a local Colibri instance is serving into the
// AI Chat ModelService, mirroring OllamaModelFetcher's shape. Unlike Ollama
// (an always-external app the browser only ever talks to), Colibri is
// launched by the browser itself (see ColibriProcessManager) - this class
// only handles the "what model is it serving" half; nothing here starts or
// stops the process.
//
// Colibri's OpenAI-compatible `GET /v1/models` doesn't expose per-model
// context length or modality info the way Ollama's `/api/show` does, so
// unlike OllamaModelFetcher this is single-phase - one model list fetch
// directly produces custom models with default settings, which the user can
// refine via the normal "Edit" pencil icon like any other custom model.
class ColibriModelFetcher {
 public:
  // Delegate interface for Colibri API operations, decoupling this class
  // from ColibriService the same way OllamaModelFetcher::Delegate does.
  class Delegate {
   public:
    using ModelsCallback =
        base::OnceCallback<void(std::optional<std::vector<std::string>>)>;

    virtual ~Delegate() = default;

    // Fetch the model id(s) currently served by Colibri (GET /v1/models).
    virtual void FetchModels(ModelsCallback callback) = 0;
  };

  ColibriModelFetcher(ModelService& model_service,
                      PrefService* prefs,
                      Delegate* delegate);
  ~ColibriModelFetcher();

  ColibriModelFetcher(const ColibriModelFetcher&) = delete;
  ColibriModelFetcher& operator=(const ColibriModelFetcher&) = delete;

  // Set the delegate for Colibri API operations. This must be called before
  // the fetcher is used if constructed with a nullptr delegate.
  void SetDelegate(Delegate* delegate);

  // Fetches and syncs immediately, regardless of the
  // kBraveAIChatColibriFetchEnabled pref - called right after Colibri is
  // successfully started, so a model becomes usable in AI Chat without the
  // user having to separately flip the sync toggle first.
  void SyncNow();

 private:
  friend class ColibriModelFetcherTest;
  FRIEND_TEST_ALL_PREFIXES(ColibriModelFetcherTest, FetchModelsAddsNewModels);
  FRIEND_TEST_ALL_PREFIXES(ColibriModelFetcherTest,
                           FetchModelsRemovesObsoleteModels);
  FRIEND_TEST_ALL_PREFIXES(ColibriModelFetcherTest,
                           FetchModelsHandlesEmptyResponse);

  void OnColibriFetchEnabledChanged();
  void OnModelsFetched(std::optional<std::vector<std::string>> models);

  const raw_ref<ModelService> model_service_;
  raw_ptr<PrefService> prefs_;
  raw_ptr<Delegate> delegate_ = nullptr;
  PrefChangeRegistrar pref_change_registrar_;
  base::WeakPtrFactory<ColibriModelFetcher> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_MODEL_FETCHER_H_

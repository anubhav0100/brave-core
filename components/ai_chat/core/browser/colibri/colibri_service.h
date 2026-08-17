/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_SERVICE_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/ai_chat/core/browser/colibri/colibri_model_fetcher.h"
#include "brave/components/ai_chat/core/common/mojom/colibri.mojom.h"
#include "components/keyed_service/core/keyed_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"

namespace network {
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

namespace ai_chat {

// Handles network communication with a local Colibri instance.
// Implements the mojom::ColibriService interface for UI communication.
// Also implements ColibriModelFetcher::Delegate to provide model fetching
// capabilities. Mirrors OllamaService's shape.
class ColibriService : public KeyedService,
                       public mojom::ColibriService,
                       public ColibriModelFetcher::Delegate {
 public:
  using ModelsCallback = ColibriModelFetcher::Delegate::ModelsCallback;

  ColibriService(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::unique_ptr<ColibriModelFetcher> model_fetcher);
  ~ColibriService() override;

  ColibriService(const ColibriService&) = delete;
  ColibriService& operator=(const ColibriService&) = delete;

  // Bind a receiver for the ColibriService interface
  void BindReceiver(mojo::PendingReceiver<mojom::ColibriService> receiver);

  // Fetches Colibri's currently-served model(s) and syncs them into
  // ModelService right away, regardless of the sync-enabled pref - called
  // once Colibri has just been started successfully.
  void SyncModelsNow();

  // KeyedService implementation:
  void Shutdown() override;

  // mojom::ColibriService implementation:
  void IsConnected(IsConnectedCallback callback) override;

  // ColibriModelFetcher::Delegate implementation:
  void FetchModels(ModelsCallback callback) override;

 private:
  void OnConnectionCheckComplete(
      IsConnectedCallback callback,
      std::unique_ptr<network::SimpleURLLoader> loader,
      std::optional<std::string> response);

  void OnModelsListComplete(ModelsCallback callback,
                            std::unique_ptr<network::SimpleURLLoader> loader,
                            std::optional<std::string> response);

  std::optional<std::vector<std::string>> ParseModelsResponse(
      const std::string& response_body);

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  mojo::ReceiverSet<mojom::ColibriService> receivers_;
  std::unique_ptr<ColibriModelFetcher> model_fetcher_;
  base::WeakPtrFactory<ColibriService> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_COLIBRI_COLIBRI_SERVICE_H_

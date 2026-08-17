/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/ai_chat/core/browser/colibri/colibri_model_fetcher.h"

#include <utility>

#include "base/task/sequenced_task_runner.h"
#include "brave/components/ai_chat/core/browser/model_service.h"
#include "brave/components/ai_chat/core/common/constants.h"
#include "brave/components/ai_chat/core/common/mojom/colibri.mojom.h"
#include "brave/components/ai_chat/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

// Colibri's `/v1/models` doesn't report context length the way Ollama's
// `/api/show` does - this is just a reasonable starting point the user can
// correct via the model's own "Edit" screen once they know their build's
// real KV configuration (see `coli serve --kv-slots`, docs/api.md).
constexpr uint32_t kDefaultContextSize = 8192;

}  // namespace

ColibriModelFetcher::ColibriModelFetcher(ModelService& model_service,
                                         PrefService* prefs,
                                         Delegate* delegate)
    : model_service_(model_service), prefs_(prefs), delegate_(delegate) {
  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      prefs::kBraveAIChatColibriFetchEnabled,
      base::BindRepeating(&ColibriModelFetcher::OnColibriFetchEnabledChanged,
                          weak_ptr_factory_.GetWeakPtr()));

  if (prefs_->GetBoolean(prefs::kBraveAIChatColibriFetchEnabled)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ColibriModelFetcher::SyncNow,
                                  weak_ptr_factory_.GetWeakPtr()));
  }
}

ColibriModelFetcher::~ColibriModelFetcher() = default;

void ColibriModelFetcher::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
}

void ColibriModelFetcher::OnColibriFetchEnabledChanged() {
  if (prefs_->GetBoolean(prefs::kBraveAIChatColibriFetchEnabled)) {
    SyncNow();
  }
}

void ColibriModelFetcher::SyncNow() {
  if (!delegate_) {
    return;
  }
  delegate_->FetchModels(base::BindOnce(
      &ColibriModelFetcher::OnModelsFetched, weak_ptr_factory_.GetWeakPtr()));
}

void ColibriModelFetcher::OnModelsFetched(
    std::optional<std::vector<std::string>> models) {
  if (!models) {
    return;
  }

  const auto& existing_models = model_service_->GetCustomModels();
  std::set<std::string> existing_colibri_model_names;
  for (const auto& existing_model : existing_models) {
    if (existing_model->options &&
        existing_model->options->is_custom_model_options() &&
        existing_model->options->get_custom_model_options() &&
        existing_model->options->get_custom_model_options()
            ->endpoint.is_valid() &&
        existing_model->options->get_custom_model_options()->endpoint.spec() ==
            mojom::kColibriEndpoint) {
      existing_colibri_model_names.insert(
          existing_model->options->get_custom_model_options()
              ->model_request_name);
    }
  }

  std::set<std::string> current_colibri_models(models->begin(),
                                               models->end());

  model_service_->MaybeDeleteCustomModels(base::BindRepeating(
      [](const std::set<std::string>& current_models,
         const base::DictValue& model_dict) {
        const std::string* endpoint_str =
            model_dict.FindString(kCustomModelItemEndpointUrlKey);
        const std::string* model_name =
            model_dict.FindString(kCustomModelItemModelKey);
        return endpoint_str && model_name &&
               GURL(*endpoint_str) == GURL(mojom::kColibriEndpoint) &&
               !current_models.contains(*model_name);
      },
      current_colibri_models));

  for (const auto& model_name : *models) {
    if (existing_colibri_model_names.contains(model_name)) {
      continue;
    }

    auto custom_model = mojom::Model::New();
    custom_model->key = "";
    custom_model->display_name = model_name;
    custom_model->vision_support = false;
    custom_model->supports_tools = false;
    custom_model->is_suggested_model = false;

    auto custom_options = mojom::CustomModelOptions::New();
    custom_options->model_request_name = model_name;
    custom_options->endpoint = GURL(mojom::kColibriEndpoint);
    custom_options->api_key = "";
    custom_options->context_size = kDefaultContextSize;

    custom_model->options =
        mojom::ModelOptions::NewCustomModelOptions(std::move(custom_options));

    model_service_->AddCustomModel(std::move(custom_model));
  }
}

}  // namespace ai_chat

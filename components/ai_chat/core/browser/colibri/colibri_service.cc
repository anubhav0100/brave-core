/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/ai_chat/core/browser/colibri/colibri_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "brave/components/ai_chat/core/browser/colibri/colibri_model_fetcher.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace ai_chat {

namespace {

constexpr net::NetworkTrafficAnnotationTag kColibriConnectionAnnotation =
    net::DefineNetworkTrafficAnnotation(
        "brave_leo_assistant_colibri_connection",
        R"(
        semantics {
          sender: "Brave Leo Assistant"
          description:
            "Check if a browser-launched local Colibri instance is "
            "running, to enable fetching its served model."
          trigger:
            "User accesses Leo Assistant settings with Colibri fetching."
          data:
            "HTTP request to 127.0.0.1:8000 to check Colibri availability."
          destination: LOCAL
        }
        policy {
          cookies_allowed: NO
          setting: "This feature can be controlled in Leo Assistant settings."
        })");

constexpr net::NetworkTrafficAnnotationTag kColibriModelsAnnotation =
    net::DefineNetworkTrafficAnnotation("brave_leo_assistant_colibri_models",
                                        R"(
        semantics {
          sender: "Brave Leo Assistant"
          description:
            "Fetch the model(s) served by a browser-launched local Colibri "
            "instance, for use in chat."
          trigger:
            "User enables Colibri fetching in Leo Assistant settings, or "
            "starts Colibri from that page."
          data:
            "HTTP request to 127.0.0.1:8000/v1/models for the model list."
          destination: LOCAL
        }
        policy {
          cookies_allowed: NO
          setting: "This feature can be disabled in Leo Assistant settings."
        })");

constexpr size_t kConnectionCheckMaxSize = 1024;
constexpr size_t kModelListMaxSize = 1024 * 1024;

}  // namespace

ColibriService::ColibriService(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::unique_ptr<ColibriModelFetcher> model_fetcher)
    : url_loader_factory_(std::move(url_loader_factory)),
      model_fetcher_(std::move(model_fetcher)) {
  if (model_fetcher_) {
    model_fetcher_->SetDelegate(this);
  }
}

ColibriService::~ColibriService() = default;

void ColibriService::Shutdown() {
  model_fetcher_.reset();
}

void ColibriService::BindReceiver(
    mojo::PendingReceiver<mojom::ColibriService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void ColibriService::SyncModelsNow() {
  if (model_fetcher_) {
    model_fetcher_->SyncNow();
  }
}

void ColibriService::IsConnected(IsConnectedCallback callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(mojom::kColibriBaseUrl);
  request->method = net::HttpRequestHeaders::kGetMethod;

  auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                 kColibriConnectionAnnotation);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ColibriService::OnConnectionCheckComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(loader)),
      kConnectionCheckMaxSize);
}

void ColibriService::OnConnectionCheckComplete(
    IsConnectedCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response) {
  // Colibri has no dedicated root route, but it still answers with a 404
  // (rather than refusing the connection) once its HTTP server is up - any
  // response at all means something is listening on the port.
  bool connected = loader->ResponseInfo() && loader->ResponseInfo()->headers;
  std::move(callback).Run(connected);
}

void ColibriService::FetchModels(ModelsCallback callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(mojom::kColibriListModelsAPIEndpoint);
  request->method = net::HttpRequestHeaders::kGetMethod;

  auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                 kColibriModelsAnnotation);

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ColibriService::OnModelsListComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(loader)),
      kModelListMaxSize);
}

void ColibriService::OnModelsListComplete(
    ModelsCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response) {
  if (!response) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(ParseModelsResponse(*response));
}

std::optional<std::vector<std::string>> ColibriService::ParseModelsResponse(
    const std::string& response_body) {
  // OpenAI-shaped: {"object":"list","data":[{"id":"glm-5.2-colibri", ...}]}
  std::optional<base::DictValue> json_dict = base::JSONReader::ReadDict(
      response_body, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!json_dict) {
    return std::nullopt;
  }

  const base::ListValue* data_list = json_dict->FindList("data");
  if (!data_list) {
    return std::nullopt;
  }

  std::vector<std::string> models;
  for (const auto& model : *data_list) {
    const base::DictValue* model_dict = model.GetIfDict();
    if (!model_dict) {
      continue;
    }
    const std::string* model_id = model_dict->FindString("id");
    if (!model_id) {
      continue;
    }
    models.push_back(*model_id);
  }

  return models;
}

}  // namespace ai_chat

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/leadflow/leadflow_api_client.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/types/expected.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace ai_chat::leadflow {

namespace {

constexpr char kBaseUrlPref[] = "brave.leadflow.api_base_url";
constexpr char kApiKeyPref[] = "brave.leadflow.api_key";

net::NetworkTrafficAnnotationTag GetTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("ai_chat_leadflow_tool", R"(
      semantics {
        sender: "AI Chat LeadFlow Tool"
        description:
          "Calls a separately self-hosted LeadFlow API server (a Dockerized "
          "lead-management/WhatsApp-messaging service the user runs "
          "themselves, entirely outside of Brave) on the AI Assistant's "
          "behalf, using an API key the user's own server issued."
        trigger:
          "The AI Assistant decides to use a LeadFlow tool (add_lead, "
          "search_leads, send a WhatsApp message, etc.) while responding "
          "in a conversation."
        data: "Lead/messaging data as chosen by the AI Assistant."
        destination: OTHER
        destination_other: "The LeadFlow API server URL the user configured."
        internal {
          contacts {
            email: "ai-chat@brave.com"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-09-25"
      }
      policy {
        cookies_allowed: NO
        setting:
          "This feature is only used if the AI Assistant is explicitly "
          "asked to configure or use a LeadFlow API connection."
        policy_exception_justification:
          "Not covered by a dedicated policy - the user controls this by "
          "choosing whether to run the LeadFlow server and connect it at "
          "all."
      })");
}

}  // namespace

LeadFlowApiClient::LeadFlowApiClient(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  api_request_helper_ = std::make_unique<api_request_helper::APIRequestHelper>(
      GetTrafficAnnotationTag(),
      browser_context_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess());
}

LeadFlowApiClient::~LeadFlowApiClient() = default;

// static
void LeadFlowApiClient::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kBaseUrlPref, "http://localhost:5080");
  registry->RegisterStringPref(kApiKeyPref, "");
}

std::string LeadFlowApiClient::GetBaseUrl() const {
  return user_prefs::UserPrefs::Get(browser_context_)->GetString(kBaseUrlPref);
}

std::string LeadFlowApiClient::GetApiKey() const {
  return user_prefs::UserPrefs::Get(browser_context_)->GetString(kApiKeyPref);
}

bool LeadFlowApiClient::IsConfigured() const {
  return !GetApiKey().empty();
}

void LeadFlowApiClient::SetBaseUrl(const std::string& base_url) {
  user_prefs::UserPrefs::Get(browser_context_)->SetString(kBaseUrlPref, base_url);
}

void LeadFlowApiClient::SetApiKey(const std::string& api_key) {
  user_prefs::UserPrefs::Get(browser_context_)->SetString(kApiKeyPref, api_key);
}

void LeadFlowApiClient::Bootstrap(
    const std::string& base_url,
    base::OnceCallback<void(bool, std::string)> callback) {
  GURL url(base::StrCat({base_url, "/api/v1/admin/bootstrap-key"}));
  if (!url.is_valid()) {
    std::move(callback).Run(false, "Invalid base URL.");
    return;
  }

  user_prefs::UserPrefs::Get(browser_context_)->SetString(kBaseUrlPref, base_url);

  api_request_helper_->Request(
      "POST", url, "", "application/json",
      base::BindOnce(
          [](base::WeakPtr<LeadFlowApiClient> self,
             base::OnceCallback<void(bool, std::string)> callback,
             api_request_helper::APIRequestResult result) {
            if (!self) {
              return;
            }
            self->OnBootstrapResponse(
                std::move(callback),
                result.Is2XXResponseCode()
                    ? base::expected<base::Value, std::string>(
                          std::move(result).TakeBody())
                    : base::unexpected(base::StrCat(
                          {"HTTP ", base::NumberToString(
                                        result.response_code())})));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void LeadFlowApiClient::OnBootstrapResponse(
    base::OnceCallback<void(bool, std::string)> callback,
    base::expected<base::Value, std::string> result) {
  if (!result.has_value()) {
    std::move(callback).Run(
        false, base::StrCat({"Bootstrap failed: ", result.error()}));
    return;
  }
  const std::string* key =
      result->is_dict() ? result->GetDict().FindString("plaintextKey") : nullptr;
  if (!key) {
    std::move(callback).Run(
        false,
        "Bootstrap succeeded but no key was returned - an API key may "
        "already exist on this server. Ask the user for one, or call "
        "POST /api/v1/admin/api-clients with an existing key.");
    return;
  }
  user_prefs::UserPrefs::Get(browser_context_)->SetString(kApiKeyPref, *key);
  std::move(callback).Run(true, *key);
}

void LeadFlowApiClient::Request(const std::string& method,
                                const std::string& path,
                                std::optional<base::Value> body,
                                JsonResultCallback callback) {
  if (!IsConfigured()) {
    std::move(callback).Run(
        false,
        base::Value(
            "Error: no LeadFlow API connection is configured yet. Call "
            "leadflow_configure_api with the server's base URL first."));
    return;
  }

  GURL url(base::StrCat({GetBaseUrl(), path}));
  if (!url.is_valid()) {
    std::move(callback).Run(false, base::Value("Error: invalid LeadFlow API URL."));
    return;
  }

  std::string payload;
  if (body.has_value()) {
    base::JSONWriter::Write(*body, &payload);
  }

  base::flat_map<std::string, std::string> headers;
  headers.emplace("X-Api-Key", GetApiKey());

  api_request_helper_->Request(
      method, url, payload, "application/json",
      base::BindOnce(
          [](base::WeakPtr<LeadFlowApiClient> self, JsonResultCallback callback,
             api_request_helper::APIRequestResult result) {
            if (!self) {
              return;
            }
            bool ok = result.Is2XXResponseCode();
            base::Value value = ok
                ? std::move(result).TakeBody()
                : base::Value(base::StrCat(
                      {"Error: LeadFlow API returned HTTP ",
                       base::NumberToString(result.response_code()),
                       result.value_body().is_none()
                           ? ""
                           : base::StrCat({": ", [&result] {
                               std::string s;
                               base::JSONWriter::Write(result.value_body(), &s);
                               return s;
                             }()})}));
            std::move(callback).Run(ok, std::move(value));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      headers);
}

}  // namespace ai_chat::leadflow

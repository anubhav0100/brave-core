// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_API_CLIENT_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_API_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "base/values.h"

class PrefRegistrySimple;

namespace content {
class BrowserContext;
}  // namespace content

namespace api_request_helper {
class APIRequestHelper;
}  // namespace api_request_helper

namespace ai_chat::leadflow {

// Connection to a separately-hosted LeadFlow API server (see
// marketing-panel-spec.md and leadflow-api/ at the repo root) - a
// standalone, Dockerized .NET service the AI Assistant talks to over plain
// HTTP + an API key, entirely independent of the browser process. The base
// URL and key are stored per-profile via PrefService (mirroring
// ComputerUseSessionState/LeadResearchState's own persisted settings), set
// once by ConfigureLeadflowApiTool rather than through any dedicated
// settings UI - this feature is AI-tool-driven only, with no browser panel.
class LeadFlowApiClient {
 public:
  using JsonResultCallback =
      base::OnceCallback<void(bool success, base::Value result_or_error)>;

  explicit LeadFlowApiClient(content::BrowserContext* browser_context);
  ~LeadFlowApiClient();
  LeadFlowApiClient(const LeadFlowApiClient&) = delete;
  LeadFlowApiClient& operator=(const LeadFlowApiClient&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  std::string GetBaseUrl() const;
  std::string GetApiKey() const;
  bool IsConfigured() const;
  void SetBaseUrl(const std::string& base_url);
  void SetApiKey(const std::string& api_key);

  // Performs POST {base_url}/api/v1/admin/bootstrap-key (no API key
  // required - see BootstrapController's own comment on why this is safe:
  // it only ever succeeds once, on a completely fresh database). Persists
  // the returned key via PrefService on success.
  void Bootstrap(const std::string& base_url,
                 base::OnceCallback<void(bool, std::string)> callback);

  // Performs an authenticated JSON request against
  // {configured base_url}{path}. Fails immediately, with no network call,
  // if no base URL/key is configured yet (callers should tell the model to
  // run configure_leadflow_api first). `body` is serialized as the JSON
  // request payload for POST/PUT/PATCH; ignored for GET.
  void Request(const std::string& method,
              const std::string& path,
              std::optional<base::Value> body,
              JsonResultCallback callback);

 private:
  void OnBootstrapResponse(base::OnceCallback<void(bool, std::string)> callback,
                          base::expected<base::Value, std::string> result);

  raw_ptr<content::BrowserContext> browser_context_;
  std::unique_ptr<api_request_helper::APIRequestHelper> api_request_helper_;
  base::WeakPtrFactory<LeadFlowApiClient> weak_ptr_factory_{this};
};

}  // namespace ai_chat::leadflow

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_LEADFLOW_LEADFLOW_API_CLIENT_H_

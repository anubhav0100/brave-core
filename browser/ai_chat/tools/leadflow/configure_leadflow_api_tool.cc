// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/leadflow/configure_leadflow_api_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"

namespace ai_chat {

namespace {
constexpr char kPropertyBaseUrl[] = "base_url";
constexpr char kPropertyApiKey[] = "api_key";
constexpr char kDefaultBaseUrl[] = "http://localhost:5080";
}  // namespace

ConfigureLeadflowApiTool::ConfigureLeadflowApiTool(
    content::BrowserContext* browser_context)
    : client_(std::make_unique<leadflow::LeadFlowApiClient>(browser_context)) {}

ConfigureLeadflowApiTool::~ConfigureLeadflowApiTool() = default;

std::string_view ConfigureLeadflowApiTool::Name() const {
  return mojom::kLeadflowConfigureApiToolName;
}

std::string_view ConfigureLeadflowApiTool::Description() const {
  return "Connects the other leadflow_* tools to a running LeadFlow API "
         "server (a separately self-hosted lead-management/WhatsApp "
         "messaging service). Call this once before using any other "
         "leadflow_* tool. Omit api_key on a fresh server to create the "
         "first key automatically; pass an existing key to reconnect to a "
         "server that's already been set up.";
}

std::optional<base::DictValue> ConfigureLeadflowApiTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyBaseUrl,
       StringProperty(
           "Base URL of the LeadFlow API server, e.g. http://localhost:5080 "
           "(the default if omitted).")},
      {kPropertyApiKey,
       StringProperty(
           "An existing LeadFlow API key (starts with lf_live_), if one was "
           "already issued. Omit to bootstrap a brand new key instead.")},
  });
}

std::optional<std::vector<std::string>>
ConfigureLeadflowApiTool::RequiredProperties() const {
  return std::nullopt;
}

void ConfigureLeadflowApiTool::UseTool(const std::string& input_json,
                                       UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* base_url_ptr =
      input ? input->FindString(kPropertyBaseUrl) : nullptr;
  const std::string* api_key_ptr =
      input ? input->FindString(kPropertyApiKey) : nullptr;
  std::string base_url = base_url_ptr && !base_url_ptr->empty()
                             ? *base_url_ptr
                             : kDefaultBaseUrl;
  // A trailing slash would produce a doubled "//api/v1/..." path once
  // concatenated with each tool's own leading-slash path.
  while (!base_url.empty() && base_url.back() == '/') {
    base_url.pop_back();
  }

  if (api_key_ptr && !api_key_ptr->empty()) {
    client_->SetBaseUrl(base_url);
    client_->SetApiKey(*api_key_ptr);
    client_->Request(
        "GET", "/api/v1/health", std::nullopt,
        base::BindOnce(&ConfigureLeadflowApiTool::OnHealthCheckResult,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                       base_url));
    return;
  }

  client_->Bootstrap(
      base_url, base::BindOnce(&ConfigureLeadflowApiTool::OnBootstrapResult,
                               weak_ptr_factory_.GetWeakPtr(),
                               std::move(callback)));
}

void ConfigureLeadflowApiTool::OnHealthCheckResult(UseToolCallback callback,
                                                   std::string base_url,
                                                   bool success,
                                                   base::Value result) {
  if (!success) {
    std::move(callback).Run(
        CreateContentBlocksForText(base::StrCat(
            {"Error: could not reach LeadFlow API at ", base_url, " with "
             "the given key: ",
             result.is_string() ? result.GetString() : ""})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Connected to LeadFlow API at ", base_url,
           ". The leadflow_* tools are ready to use."})),
      {});
}

void ConfigureLeadflowApiTool::OnBootstrapResult(UseToolCallback callback,
                                                 bool success,
                                                 std::string message) {
  std::move(callback).Run(
      CreateContentBlocksForText(
          success
              ? base::StrCat(
                    {"Connected. A new LeadFlow API key was created and "
                     "stored: ",
                     message,
                     " - keep a copy of this key somewhere safe; the "
                     "server won't show it again."})
              : message),
      {});
}

}  // namespace ai_chat

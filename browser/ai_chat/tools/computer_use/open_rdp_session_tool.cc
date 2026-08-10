// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/computer_use/open_rdp_session_tool.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "brave/browser/computer_use/computer_use_session_state.h"
#include "brave/browser/computer_use/computer_use_session_state_factory.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "content/public/browser/browser_context.h"

namespace ai_chat {

namespace {
constexpr char kPropertyHost[] = "host";
constexpr char kPropertyPort[] = "port";
constexpr int kDefaultRdpPort = 3389;

std::string ExtractHost(const std::string& arguments_json) {
  auto input = base::JSONReader::ReadDict(arguments_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* host = input ? input->FindString(kPropertyHost) : nullptr;
  return host ? *host : std::string();
}
}  // namespace

OpenRdpSessionTool::OpenRdpSessionTool(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

OpenRdpSessionTool::~OpenRdpSessionTool() = default;

std::string_view OpenRdpSessionTool::Name() const {
  return mojom::kOpenRdpSessionToolName;
}

std::string_view OpenRdpSessionTool::Description() const {
  return "Opens a visible RDP (Remote Desktop) session to a host the user "
         "administers, in its own window. Requires the user's explicit "
         "confirmation naming the target host every single time - never "
         "silently autonomous, regardless of any prior permission grants. "
         "Never provide a password - authentication is handled by the RDP "
         "client's own prompt or the user's saved Windows credentials for "
         "that host. Once connected, get_desktop_screenshot and the "
         "desktop_* input tools work on the RDP window like any other "
         "on-screen window.";
}

bool OpenRdpSessionTool::IsAgentTool() const {
  return true;
}

std::optional<base::DictValue> OpenRdpSessionTool::InputProperties() const {
  return CreateInputProperties({
      {kPropertyHost, StringProperty("Hostname or IP address to connect to")},
      {kPropertyPort,
       IntegerProperty("RDP port - defaults to 3389 if not specified")},
  });
}

std::optional<std::vector<std::string>>
OpenRdpSessionTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyHost});
}

std::variant<bool, mojom::PermissionChallengePtr>
OpenRdpSessionTool::RequiresUserInteractionBeforeHandling(
    const mojom::ToolUseEvent& tool_use) const {
  // Deliberately unconditional: every open_rdp_session call gets a fresh
  // challenge, unlike the desktop_* tools' one-time-then-remembered
  // consent. See brave-ai-computer-use.md's safety architecture #4.
  std::string host = ExtractHost(tool_use.arguments_json);
  std::string plan = host.empty()
                         ? "Connect to a remote host via RDP."
                         : base::StrCat({"Connect to \"", host, "\" via RDP."});
  return mojom::PermissionChallenge::New(/*assessment=*/std::nullopt, plan);
}

void OpenRdpSessionTool::UserPermissionGranted(
    const std::string& tool_use_id) {
  // Nothing to remember - see RequiresUserInteractionBeforeHandling.
}

void OpenRdpSessionTool::UseTool(const std::string& input_json,
                                 UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const std::string* host = input ? input->FindString(kPropertyHost) : nullptr;
  if (!host || host->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing 'host'"), {});
    return;
  }
  int port = input ? input->FindInt(kPropertyPort).value_or(kDefaultRdpPort)
                   : kDefaultRdpPort;

  computer_use::ComputerUseSessionStateFactory::GetForBrowserContext(
      browser_context_)
      ->ConnectRdp(*host, port,
                  base::BindOnce(&OpenRdpSessionTool::OnConnectResult,
                                 weak_ptr_factory_.GetWeakPtr(),
                                 std::move(callback)));
}

void OpenRdpSessionTool::OnConnectResult(UseToolCallback callback,
                                         bool success,
                                         std::string error_message) {
  std::move(callback).Run(
      CreateContentBlocksForText(
          success ? "RDP session connected. A window showing the remote "
                    "desktop is now visible to the user."
                 : base::StrCat({"Error: RDP connection failed: ",
                                error_message})),
      {});
}

}  // namespace ai_chat

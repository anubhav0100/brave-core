// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/n8n/n8n_process_manager.h"

#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace ai_chat {

namespace {

constexpr char kN8nApiKeyPref[] = "brave.n8n.api_key";

// n8n's own default port - kept fixed rather than finding a free port so
// the side panel and REST-API tools can build URLs without a round trip to
// ask the process manager first.
constexpr int kN8nPort = 5678;

// n8n takes a few seconds to boot its web server after the process starts
// (dependency loading, its own SQLite migrations, etc.) - see the header
// comment on EnsureStarted for why this is a fixed delay rather than a real
// health-check poll.
constexpr base::TimeDelta kStartupGracePeriod = base::Seconds(5);

}  // namespace

// static
void N8nProcessManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kN8nApiKeyPref, std::string());
}

// static
void N8nProcessManager::SetApiKey(PrefService* prefs,
                                  const std::string& api_key) {
  prefs->SetString(kN8nApiKeyPref, api_key);
}

// static
std::string N8nProcessManager::GetApiKey(PrefService* prefs) {
  return prefs->GetString(kN8nApiKeyPref);
}

// static
base::FilePath N8nProcessManager::GetDataDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &user_data_dir);
  return user_data_dir.Append(FILE_PATH_LITERAL("BraveN8nData"));
}

N8nProcessManager::N8nProcessManager() = default;

N8nProcessManager::~N8nProcessManager() {
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

bool N8nProcessManager::IsRunning() const {
  return process_.IsValid();
}

void N8nProcessManager::EnsureStarted(StartedCallback callback) {
  if (IsRunning()) {
    std::move(callback).Run(true);
    return;
  }

  // `npx` is a .cmd wrapper on Windows and can't be launched directly via
  // CreateProcess - route through cmd.exe /c the same way pnpm/npm's own
  // scripts do in this project's build tooling.
  base::CommandLine cmd(base::FilePath(FILE_PATH_LITERAL("cmd.exe")));
  cmd.AppendArg("/c");
  cmd.AppendArg("npx");
  cmd.AppendArg("--yes");
  cmd.AppendArg("n8n");
  cmd.AppendArg("start");

  auto env = base::Environment::Create();
  env->SetVar("N8N_PORT", base::NumberToString(kN8nPort));
  env->SetVar("N8N_USER_FOLDER", GetDataDir().AsUTF8Unsafe());
  // Skip n8n's own first-run task-completion/diagnostics survey prompts -
  // this is being driven by the browser, not a human clicking through
  // n8n's own onboarding.
  env->SetVar("N8N_DIAGNOSTICS_ENABLED", "false");
  env->SetVar("N8N_VERSION_NOTIFICATIONS_ENABLED", "false");

  base::LaunchOptions options;
  process_ = base::LaunchProcess(cmd, options);
  if (!process_.IsValid()) {
    std::move(callback).Run(false);
    return;
  }

  port_ = kN8nPort;
  base_url_ = base::StrCat({"http://localhost:", base::NumberToString(port_)});

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<N8nProcessManager> self,
             StartedCallback callback) {
            if (!self) {
              std::move(callback).Run(false);
              return;
            }
            std::move(callback).Run(self->IsRunning());
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
      kStartupGracePeriod);
}

void N8nProcessManager::Shutdown() {
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
  }
}

}  // namespace ai_chat

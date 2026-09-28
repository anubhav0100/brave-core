// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/computer_use/action_risk_classifier.h"

#include <windows.h>

#include <array>
#include <string>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/computer_use/computer_use_session_state.h"

namespace computer_use {

namespace {

std::string GetProcessNameForWindow(HWND hwnd) {
  if (!hwnd) {
    return std::string();
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) {
    return std::string();
  }
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return std::string();
  }
  wchar_t path[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  std::string result;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) {
    std::wstring wpath(path, size);
    size_t slash = wpath.find_last_of(L"\\/");
    std::wstring filename =
        slash == std::wstring::npos ? wpath : wpath.substr(slash + 1);
    result = base::ToLowerASCII(base::WideToUTF8(filename));
  }
  CloseHandle(process);
  return result;
}

// Process image names of utilities that can do broad, hard-to-undo damage
// (delete/format/registry/system-config/privilege changes) - any action
// targeting one of these always requires confirmation, regardless of
// whether the AI has "seen" this app before this session.
constexpr std::array<const char*, 21> kSensitiveProcessNames = {
    "cmd.exe",      "powershell.exe", "powershell_ise.exe",
    "pwsh.exe",     "windowsterminal.exe", "regedit.exe",
    "diskpart.exe", "format.com",     "mmc.exe",
    "control.exe",  "msconfig.exe",   "bcdedit.exe",
    "cipher.exe",   "takeown.exe",    "icacls.exe",
    "sc.exe",       "net.exe",        "net1.exe",
    "wmic.exe",     "certutil.exe",   "vssadmin.exe",
};

// Substrings (checked case-insensitively) that flag typed text or a key
// combo as risky regardless of target app - covers destructive commands
// typed into an otherwise-unremarkable window (e.g. a browser address bar
// or a text editor that then gets run/piped somewhere).
constexpr std::array<const char*, 17> kSensitiveKeywords = {
    "format",  "delete",   "del /",   "rm -rf",     "rmdir /s",
    "uninstall", "diskpart", "shutdown", "sudo",     "regedit",
    "reg delete", "net user", "taskkill", "bcdedit", "vssadmin",
    "takeown", "cipher /w",
};

// Common office/productivity apps exempted from the "first time touching
// this app" check below - editing a document/spreadsheet/presentation is
// routine, not inherently risky, so re-confirming on first contact is pure
// friction for these specifically. Apps NOT on this list (a banking app, a
// password manager, anything neither this list nor kSensitiveProcessNames
// anticipated) still get the first-contact confirmation - this carve-out
// is deliberately narrow rather than a blanket "trust everything" change.
constexpr std::array<const char*, 11> kTrustedProductivityProcessNames = {
    "winword.exe", "excel.exe",     "powerpnt.exe", "onenote.exe",
    "outlook.exe", "notepad.exe",   "notepad++.exe", "wordpad.exe",
    "acrobat.exe", "acrord32.exe",  "soffice.bin",
};

}  // namespace

std::string GetProcessNameAtPoint(int x, int y) {
  return GetProcessNameForWindow(WindowFromPoint({x, y}));
}

std::string GetForegroundProcessName() {
  return GetProcessNameForWindow(GetForegroundWindow());
}

RiskAssessment ClassifyDesktopAction(const std::string& process_name,
                                     const std::string& action_detail,
                                     ComputerUseSessionState* state) {
  for (const char* sensitive : kSensitiveProcessNames) {
    if (process_name == sensitive) {
      return {true, "targets a sensitive system utility (" + process_name +
                        ")"};
    }
  }

  std::string lower_detail = base::ToLowerASCII(action_detail);
  for (const char* keyword : kSensitiveKeywords) {
    if (lower_detail.find(keyword) != std::string::npos) {
      return {true,
             "typed content or key combo matches a sensitive keyword (\"" +
                 std::string(keyword) + "\")"};
    }
  }

  bool is_trusted_productivity_app = false;
  for (const char* trusted : kTrustedProductivityProcessNames) {
    if (process_name == trusted) {
      is_trusted_productivity_app = true;
      break;
    }
  }

  if (!process_name.empty() && state && !is_trusted_productivity_app &&
      !state->HasInteractedWithApp(process_name)) {
    return {true,
           "first action this session directed at an app the AI hasn't "
           "used yet (" +
               process_name + ")"};
  }

  return {false, std::string()};
}

}  // namespace computer_use

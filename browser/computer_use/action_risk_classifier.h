// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_COMPUTER_USE_ACTION_RISK_CLASSIFIER_H_
#define BRAVE_BROWSER_COMPUTER_USE_ACTION_RISK_CLASSIFIER_H_

#include <string>

namespace computer_use {

class ComputerUseSessionState;

// Returns the lowercase process image name (e.g. "cmd.exe") of the window
// at the given desktop point, or an empty string if it can't be
// determined.
std::string GetProcessNameAtPoint(int x, int y);

// Returns the lowercase process image name of the current foreground
// window (whatever has keyboard focus), or empty if it can't be
// determined.
std::string GetForegroundProcessName();

struct RiskAssessment {
  bool is_risky = false;
  // Human-readable, shown to the user in the permission-challenge prompt.
  std::string reason;
};

// Classifies whether an input action targeting `process_name` (may be
// empty if unknown) should require explicit confirmation before
// executing - see brave-ai-computer-use.md, Phase 2 safety architecture
// item 3 ("Risk-classified confirmation"). `action_detail` is the typed
// text or key combo for keyboard actions, or empty for mouse actions.
// Deliberately coarse for Phase 2 (no accessibility-tree/OCR element
// awareness yet - that's a later phase): flags known-dangerous system
// utilities, sensitive keywords in typed/key content, and the first
// action against any app the AI hasn't touched yet this session.
RiskAssessment ClassifyDesktopAction(const std::string& process_name,
                                     const std::string& action_detail,
                                     ComputerUseSessionState* state);

}  // namespace computer_use

#endif  // BRAVE_BROWSER_COMPUTER_USE_ACTION_RISK_CLASSIFIER_H_

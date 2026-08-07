// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PRESENTATION_TOOL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PRESENTATION_TOOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/components/ai_chat/core/browser/tools/tool.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace ai_chat {

namespace internal {

// Builds every OOXML part for a .pptx from a "slides" JSON array (each
// entry a dict with a "title" string and an optional "body" string, whose
// "\n"-separated lines each become a bullet paragraph). Exposed for unit
// tests.
std::vector<OoxmlPart> BuildPresentationParts(const base::ListValue& slides);

}  // namespace internal

// Exposes a tool that lets the assistant create a PowerPoint (.pptx)
// presentation from a list of {title, body} slides and triggers a native
// browser download of the result. Builds a minimal OOXML package directly
// via document_download_util.h - unlike Word/Excel, PowerPoint requires a
// slide master, slide layout, and theme part even for a minimal
// presentation, so this tool's generated archive has more boilerplate
// parts than the Word/Excel tools, but the approach (temp dir + zip) is
// identical. V1 scope is intentionally limited to a title and a list of
// plain-text body lines per slide; no images or rich layouts.
class CreatePresentationTool : public Tool {
 public:
  explicit CreatePresentationTool(content::BrowserContext* browser_context);
  ~CreatePresentationTool() override;

  CreatePresentationTool(const CreatePresentationTool&) = delete;
  CreatePresentationTool& operator=(const CreatePresentationTool&) = delete;

  // Tool:
  std::string_view Name() const override;
  std::string_view Description() const override;
  std::optional<base::DictValue> InputProperties() const override;
  std::optional<std::vector<std::string>> RequiredProperties() const override;
  void UseTool(const std::string& input_json,
               UseToolCallback callback) override;

 private:
  void OnDownloadComplete(UseToolCallback callback,
                          std::string filename,
                          DocumentDownloadResult result);

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;

  base::WeakPtrFactory<CreatePresentationTool> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_CREATE_PRESENTATION_TOOL_H_

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/lead_research/open_linkedin_handoff_tool.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "brave/browser/ai_chat/tools/tab_utils.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {
constexpr char kPropertyRoleTerms[] = "role_terms";
constexpr char kPropertyCompanyTerms[] = "company_terms";
constexpr char kPropertyCampaignId[] = "campaign_id";

std::vector<std::string> ReadStringArray(const base::DictValue* input,
                                         const char* key) {
  std::vector<std::string> result;
  const base::ListValue* list = input ? input->FindList(key) : nullptr;
  if (!list) {
    return result;
  }
  for (const base::Value& item : *list) {
    if (item.is_string()) {
      result.push_back(item.GetString());
    }
  }
  return result;
}
}  // namespace

OpenLinkedInHandoffTool::OpenLinkedInHandoffTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

OpenLinkedInHandoffTool::~OpenLinkedInHandoffTool() = default;

std::string_view OpenLinkedInHandoffTool::Name() const {
  return mojom::kOpenLinkedInHandoffToolName;
}

std::string_view OpenLinkedInHandoffTool::Description() const {
  return "Opens linkedin.com in a new tab and suggests role/company "
         "search terms for the USER to type into LinkedIn's own native "
         "search - this tool never searches, reads, or automates anything "
         "inside LinkedIn itself (no API access, no scraping - LinkedIn's "
         "User Agreement restricts that). Use this to hand decision-maker "
         "research off to the user; ask them to report back company/"
         "contact names they find, then use save_lead to record them.";
}

std::optional<base::DictValue> OpenLinkedInHandoffTool::InputProperties()
    const {
  return CreateInputProperties({
      {kPropertyRoleTerms,
       ArrayProperty("Suggested job title search terms, e.g. \"IT Manager\", "
                     "\"HR Head\"",
                     StringProperty("A role/title term"))},
      {kPropertyCompanyTerms,
       ArrayProperty("Suggested company/industry search terms (optional)",
                     StringProperty("A company or industry term"))},
      {kPropertyCampaignId,
       StringProperty("The campaign this search is for (optional)")},
  });
}

std::optional<std::vector<std::string>>
OpenLinkedInHandoffTool::RequiredProperties() const {
  return std::optional<std::vector<std::string>>({kPropertyRoleTerms});
}

void OpenLinkedInHandoffTool::UseTool(const std::string& input_json,
                                      UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const base::DictValue* dict = input ? &*input : nullptr;
  std::vector<std::string> role_terms =
      ReadStringArray(dict, kPropertyRoleTerms);
  if (role_terms.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'role_terms'"),
        {});
    return;
  }
  std::vector<std::string> company_terms =
      ReadStringArray(dict, kPropertyCompanyTerms);

  content::WebContents* web_contents =
      GetActiveWebContentsFor(browser_context_);
  if (!web_contents) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: no open browser window to open a tab in."),
        {});
    return;
  }
  web_contents->OpenURL(
      {GURL("https://www.linkedin.com/"), content::Referrer(),
       WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK,
       /*is_renderer_initiated=*/false},
      /*navigation_handle_callback=*/{});

  std::string suggested = base::JoinString(role_terms, ", ");
  std::string companies_suffix;
  if (!company_terms.empty()) {
    companies_suffix =
        base::StrCat({" Company/industry terms: ",
                      base::JoinString(company_terms, ", "), "."});
  }
  std::move(callback).Run(
      CreateContentBlocksForText(base::StrCat(
          {"Opened LinkedIn. Suggested role searches: ", suggested, ".",
           companies_suffix,
           " Search LinkedIn's own UI with these terms and report back "
           "which companies/contacts look relevant - this tool cannot "
           "search or read LinkedIn itself."})),
      {});
}

}  // namespace ai_chat

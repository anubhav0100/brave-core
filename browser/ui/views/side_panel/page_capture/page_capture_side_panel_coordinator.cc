// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/side_panel/page_capture/page_capture_side_panel_coordinator.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "brave/browser/ai_chat/model_service_factory.h"
#include "brave/browser/ai_chat/tools/document_download_util.h"
#include "brave/browser/ui/views/side_panel/page_capture/page_capture_view.h"
#include "brave/components/ai_chat/content/browser/page_content_fetcher.h"
#include "brave/components/ai_chat/core/browser/model_service.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

namespace page_capture {

namespace {

constexpr char kPropertyText[] = "text";
constexpr char kPropertyHeadingLevel[] = "heading_level";

std::string FormatApiError(const ai_chat::EngineConsumer::Error& error) {
  if (error.details && error.details->status_code > 0) {
    std::string message = base::StrCat(
        {"Your model's endpoint returned HTTP ",
         base::NumberToString(error.details->status_code)});
    if (!error.details->error_type.empty()) {
      base::StrAppend(&message, {": ", error.details->error_type});
    }
    return message;
  }
  switch (error.api_error) {
    case ai_chat::mojom::APIError::InvalidAPIKey:
      return "The model rejected your API key. Check it in Settings.";
    case ai_chat::mojom::APIError::RateLimitReached:
      return "Rate limit reached on your model's endpoint. Try again "
             "shortly.";
    case ai_chat::mojom::APIError::ServiceOverloaded:
      return "Your model's service is temporarily overloaded. Try again "
             "shortly.";
    case ai_chat::mojom::APIError::InvalidEndpointURL:
      return "The server endpoint looks invalid. Check it in Settings.";
    case ai_chat::mojom::APIError::ContextLimitReached:
      return "This page is too long for your model's context limit.";
    default:
      return "Could not get a response from your model's endpoint. Check "
             "the endpoint URL and API key in Settings.";
  }
}

}  // namespace

PageCaptureSidePanelCoordinator::PageCaptureSidePanelCoordinator(
    BrowserWindowInterface* browser,
    Profile* profile)
    : browser_(browser), profile_(profile) {}

PageCaptureSidePanelCoordinator::~PageCaptureSidePanelCoordinator() = default;

void PageCaptureSidePanelCoordinator::CreateAndRegisterEntry(
    SidePanelRegistry* global_registry) {
  global_registry->Register(std::make_unique<SidePanelEntry>(
      SidePanelEntry::Key(SidePanelEntry::Id::kPageCapture),
      base::BindRepeating(&PageCaptureSidePanelCoordinator::CreateView,
                          base::Unretained(this)),
      /*default_content_width_callback=*/base::NullCallback()));
}

std::unique_ptr<views::View> PageCaptureSidePanelCoordinator::CreateView(
    SidePanelEntryScope& scope) {
  return std::make_unique<PageCaptureView>(this);
}

std::vector<AvailableModel> PageCaptureSidePanelCoordinator::GetAvailableModels()
    const {
  std::vector<AvailableModel> models;
  auto* model_service = GetModelService();
  if (!model_service) {
    return models;
  }
  for (const auto& model : model_service->GetCustomModels()) {
    models.push_back({model->key, model->display_name});
  }
  return models;
}

ai_chat::ModelService* PageCaptureSidePanelCoordinator::GetModelService()
    const {
  return ai_chat::ModelServiceFactory::GetForBrowserContext(profile_);
}

void PageCaptureSidePanelCoordinator::SetSelectedModel(std::string model_key) {
  selected_model_key_ = std::move(model_key);
}

void PageCaptureSidePanelCoordinator::Log(std::string message) {
  constexpr size_t kMaxLogEntries = 100;
  activity_log_.push_back({base::Time::Now(), std::move(message)});
  if (activity_log_.size() > kMaxLogEntries) {
    activity_log_.erase(activity_log_.begin());
  }
}

void PageCaptureSidePanelCoordinator::CaptureCurrentTab(
    CaptureCallback callback) {
  if (capture_in_progress_ || pending_engine_) {
    std::move(callback).Run(false, "A capture is already in progress.");
    return;
  }

  Browser* browser = browser_->GetBrowserForMigrationOnly();
  content::WebContents* web_contents =
      browser ? browser->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!web_contents) {
    std::move(callback).Run(false, "No active tab to capture.");
    return;
  }

  std::string title = base::UTF16ToUTF8(web_contents->GetTitle());
  std::string heading =
      title.empty() ? web_contents->GetVisibleURL().spec()
                    : base::StrCat({title, " (",
                                    web_contents->GetVisibleURL().spec(),
                                    ")"});

  capture_in_progress_ = true;
  Log(base::StrCat({"Capture started: ", heading}));
  FetchFullPageSourceRecursive(
      web_contents,
      base::BindOnce(
          &PageCaptureSidePanelCoordinator::OnFullPageSourceFetched,
          weak_ptr_factory_.GetWeakPtr(), std::move(callback), heading));
}

void PageCaptureSidePanelCoordinator::OnFullPageSourceFetched(
    CaptureCallback callback,
    std::string title,
    FullPageSource source) {
  if (source.combined_html.empty()) {
    capture_in_progress_ = false;
    Log("Capture failed: could not read any content from this page.");
    std::move(callback).Run(false, "Could not read any content from this page.");
    return;
  }

  Log(base::StrCat({"Read page source: ",
                     base::NumberToString(source.combined_html.size()),
                     " chars, ", base::NumberToString(source.images.size()),
                     " images found."}));

  std::string page_content = source.combined_html;
  if (!source.images.empty()) {
    std::string image_list = "\n\nImages found on this page:\n";
    for (const auto& image : source.images) {
      base::StrAppend(&image_list, {"- ", image.second.empty()
                                              ? image.first.spec()
                                              : base::StrCat({image.first.spec(),
                                                              " (alt: \"",
                                                              image.second,
                                                              "\")"}),
                                    "\n"});
    }
    page_content += image_list;
  }

  // Keep the full, untruncated text for the saved document even though the
  // copy sent to the model below may get truncated to fit its context limit.
  const std::string original_content = page_content;

  auto* model_service =
      ai_chat::ModelServiceFactory::GetForBrowserContext(profile_);
  if (!model_service) {
    capture_in_progress_ = false;
    Log("Capture failed: AI Chat is not available in this profile.");
    std::move(callback).Run(false, "AI Chat is not available in this profile.");
    return;
  }

  // Use whichever custom model the panel's picker has selected, if it's
  // still configured. Otherwise fall back to the default model if it's a
  // custom (Bring-Your-Own-Model) one, then the first configured custom
  // model. Leo-hosted models are deliberately never used here - this
  // feature only ever talks to a model the user configured themselves.
  std::string model_key;
  const std::vector<ai_chat::mojom::ModelPtr> custom_models =
      model_service->GetCustomModels();
  for (const auto& custom_model : custom_models) {
    if (custom_model->key == selected_model_key_) {
      model_key = custom_model->key;
      break;
    }
  }
  if (model_key.empty()) {
    if (const ai_chat::mojom::Model* default_model =
            model_service->GetModel(model_service->GetDefaultModelKey());
        default_model && default_model->options->is_custom_model_options()) {
      model_key = default_model->key;
    } else if (!custom_models.empty()) {
      model_key = custom_models[0]->key;
    }
  }

  if (model_key.empty()) {
    capture_in_progress_ = false;
    Log("Capture failed: no custom model is configured.");
    std::move(callback).Run(
        false,
        "No custom model is configured. Add one in Settings -> Leo "
        "Assistant -> Bring your own model.");
    return;
  }

  // GenerateRewriteSuggestion() was built for short user-selected snippets
  // and, unlike the full conversation path, does not itself truncate to the
  // model's configured length - so a whole page's text can exceed what the
  // endpoint accepts and come back as an opaque HTTP error. Truncate here.
  if (const ai_chat::mojom::Model* model = model_service->GetModel(model_key);
      model && model->options->is_custom_model_options()) {
    uint32_t max_length =
        model->options->get_custom_model_options()->max_associated_content_length;
    if (max_length > 0 && page_content.size() > max_length) {
      page_content =
          std::string(base::TruncateUTF8ToByteSize(page_content, max_length));
    }
  }

  pending_engine_ = model_service->GetEngineForModel(
      model_key,
      profile_->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess(),
      /*credential_manager=*/nullptr);

  pending_completion_text_.clear();

  auto* engine = pending_engine_.get();
  engine->GenerateRewriteSuggestion(
      page_content, ai_chat::mojom::ActionType::SUMMARIZE_SELECTED_TEXT,
      base::BindRepeating(
          &PageCaptureSidePanelCoordinator::OnRefinementDataReceived,
          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&PageCaptureSidePanelCoordinator::OnRefinementComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(title), original_content));
}

void PageCaptureSidePanelCoordinator::OnRefinementDataReceived(
    ai_chat::EngineConsumer::GenerationResultData result_data) {
  if (result_data.event && result_data.event->is_completion_event()) {
    pending_completion_text_ +=
        result_data.event->get_completion_event()->completion;
  }
}

void PageCaptureSidePanelCoordinator::OnRefinementComplete(
    CaptureCallback callback,
    std::string title,
    std::string original_content,
    ai_chat::EngineConsumer::GenerationResult result) {
  pending_engine_.reset();
  capture_in_progress_ = false;

  if (!result.has_value()) {
    std::string error = FormatApiError(result.error());
    Log(base::StrCat({"Refinement failed for ", title, ": ", error}));
    std::move(callback).Run(false, error);
    return;
  }

  const auto& event = result.value().event;
  if (!event || !event->is_completion_event()) {
    Log(base::StrCat({"Refinement failed for ", title,
                       ": the model did not return any text."}));
    std::move(callback).Run(false, "The model did not return any text.");
    return;
  }

  // A non-streamed response carries its text directly here; a streamed one
  // (this model's kind) carries it empty by design - use what was collected
  // from OnRefinementDataReceived() instead in that case.
  std::string refined_content = event->get_completion_event()->completion;
  if (refined_content.empty()) {
    refined_content = std::move(pending_completion_text_);
  }
  pending_completion_text_.clear();

  if (refined_content.empty()) {
    Log(base::StrCat({"Refinement failed for ", title,
                       ": the model did not return any text."}));
    std::move(callback).Run(false, "The model did not return any text.");
    return;
  }

  Log(base::StrCat({"Captured and refined: ", title}));
  session_entries_.push_back(
      {std::move(title), std::move(original_content),
       std::move(refined_content)});
  std::move(callback).Run(true, "");
}

void PageCaptureSidePanelCoordinator::SaveAsWordDocument(
    CaptureCallback callback) {
  if (session_entries_.empty()) {
    std::move(callback).Run(false, "Nothing captured yet this session.");
    return;
  }

  Browser* browser = browser_->GetBrowserForMigrationOnly();
  content::WebContents* web_contents =
      browser ? browser->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!web_contents) {
    Log("Save failed: no active tab to anchor the save dialog to.");
    std::move(callback).Run(false, "No active tab to anchor the save dialog to.");
    return;
  }

  Log(base::StrCat({"Save started (", base::NumberToString(session_entries_.size()),
                     " entries)."}));

  base::ListValue paragraphs;
  for (const auto& entry : session_entries_) {
    base::DictValue heading;
    heading.Set(kPropertyText, entry.heading);
    heading.Set(kPropertyHeadingLevel, 2);
    paragraphs.Append(std::move(heading));

    base::DictValue original_heading;
    original_heading.Set(kPropertyText, "Original Content");
    original_heading.Set(kPropertyHeadingLevel, 3);
    paragraphs.Append(std::move(original_heading));

    base::DictValue original_body;
    original_body.Set(kPropertyText, entry.original_content);
    original_body.Set(kPropertyHeadingLevel, 0);
    paragraphs.Append(std::move(original_body));

    base::DictValue refined_heading;
    refined_heading.Set(kPropertyText, "Refined Summary");
    refined_heading.Set(kPropertyHeadingLevel, 3);
    paragraphs.Append(std::move(refined_heading));

    base::DictValue refined_body;
    refined_body.Set(kPropertyText, entry.refined_content);
    refined_body.Set(kPropertyHeadingLevel, 0);
    paragraphs.Append(std::move(refined_body));
  }

  std::vector<ai_chat::OoxmlPart> parts;
  parts.push_back({"[Content_Types].xml", ai_chat::kWordContentTypesXml});
  parts.push_back({"_rels/.rels", ai_chat::kWordRootRelsXml});
  parts.push_back(
      {"word/document.xml", ai_chat::BuildWordDocumentXml(paragraphs)});

  ai_chat::BuildOoxmlArchiveAndSaveAs(
      web_contents, "page-capture-session.docx", std::move(parts),
      base::BindOnce(
          [](base::WeakPtr<PageCaptureSidePanelCoordinator> self,
             CaptureCallback callback,
             ai_chat::DocumentDownloadResult result) {
            if (self) {
              self->Log(result.success
                            ? "Save succeeded."
                            : base::StrCat({"Save failed: ",
                                            result.error_message}));
            }
            std::move(callback).Run(result.success, result.error_message);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void PageCaptureSidePanelCoordinator::ClearSession() {
  Log("Session cleared.");
  session_entries_.clear();
}

}  // namespace page_capture

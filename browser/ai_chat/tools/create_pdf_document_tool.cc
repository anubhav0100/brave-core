// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/create_pdf_document_tool.h"

#include <optional>
#include <utility>
#include <variant>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/sequence_checker.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "brave/components/ai_chat/core/browser/tools/tool_input_properties.h"
#include "brave/components/ai_chat/core/browser/tools/tool_utils.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/restricted_web_contents_delegate/restricted_web_contents_delegate.h"
#include "components/printing/browser/print_to_pdf/pdf_print_job.h"
#include "components/printing/browser/print_to_pdf/pdf_print_result.h"
#include "components/printing/browser/print_to_pdf/pdf_print_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/base/filename_util.h"
#include "printing/buildflags/buildflags.h"
#include "ui/base/page_transition_types.h"

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
#include "chrome/browser/printing/print_view_manager.h"
#else
#include "chrome/browser/printing/print_view_manager_basic.h"
#endif

namespace ai_chat {

namespace {

constexpr char kPropertyNameFilename[] = "filename";
constexpr char kPropertyNameHtmlContent[] = "html_content";

constexpr base::TimeDelta kPdfGenerationTimeout = base::Seconds(30);

#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
using ActivePrintManager = printing::PrintViewManager;
#else
using ActivePrintManager = printing::PrintViewManagerBasic;
#endif

std::optional<base::FilePath> WriteHtmlToTempFile(std::string html_content) {
  base::FilePath temp_dir;
  if (!base::GetTempDir(&temp_dir)) {
    return std::nullopt;
  }
  base::FilePath temp_path;
  if (!base::CreateTemporaryFileInDir(temp_dir, &temp_path)) {
    return std::nullopt;
  }
  base::FilePath html_path = temp_path.AddExtension(FILE_PATH_LITERAL("html"));
  if (!base::Move(temp_path, html_path)) {
    base::DeleteFile(temp_path);
    return std::nullopt;
  }
  if (!base::WriteFile(html_path, html_content)) {
    base::DeleteFile(html_path);
    return std::nullopt;
  }
  return html_path;
}

void DeleteTempFile(const base::FilePath& path) {
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

// Renders `html_content` in a hidden, never-visible WebContents and prints
// it to PDF via Chromium's real print-to-pdf pipeline. Self-deletes once
// `callback` has been run - allocate with `new`, never hold a pointer to
// this past that point. This shape (rather than storing per-call state on
// the owning Tool) is needed because a single Tool instance is long-lived
// and can in principle be asked to start another generation before a
// prior one finishes.
class PdfDocumentGenerator : public RestrictedWebContentsDelegate,
                             public content::WebContentsObserver {
 public:
  using GenerateCallback =
      base::OnceCallback<void(std::vector<uint8_t> pdf_bytes,
                              std::string error_message)>;

  PdfDocumentGenerator() = default;
  ~PdfDocumentGenerator() override = default;

  void Generate(content::BrowserContext* browser_context,
                std::string html_content,
                GenerateCallback callback) {
    callback_ = std::move(callback);
    browser_context_ = browser_context;

    timeout_timer_.Start(
        FROM_HERE, kPdfGenerationTimeout,
        base::BindOnce(&PdfDocumentGenerator::OnTimeout,
                       weak_ptr_factory_.GetWeakPtr()));

    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&WriteHtmlToTempFile, std::move(html_content)),
        base::BindOnce(&PdfDocumentGenerator::OnTempHtmlWritten,
                       weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  void OnTempHtmlWritten(std::optional<base::FilePath> temp_path) {
    if (!temp_path) {
      Finish({}, "Failed to write HTML to a temp file.");
      return;
    }
    temp_html_path_ = *temp_path;

    content::WebContents::CreateParams create_params(browser_context_);
    create_params.is_never_composited = true;
    web_contents_ = content::WebContents::Create(create_params);
    web_contents_->SetOwnerLocationForDebug(FROM_HERE);
    web_contents_->SetDelegate(this);
    Observe(web_contents_.get());

    ActivePrintManager::CreateForWebContents(web_contents_.get());

    const GURL load_url = net::FilePathToFileURL(temp_html_path_);
    web_contents_->GetController().LoadURL(load_url, content::Referrer(),
                                           ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                           std::string());
  }

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame()) {
      return;
    }
    if (!navigation_handle->HasCommitted()) {
      // Posted async: Finish() destroys the WebContents, which must not
      // happen re-entrantly from within observer notification.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&PdfDocumentGenerator::Finish,
                         weak_ptr_factory_.GetWeakPtr(),
                         std::vector<uint8_t>(),
                         "Navigation did not commit."));
    }
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    auto* print_manager =
        ActivePrintManager::FromWebContents(web_contents_.get());
    if (!print_manager) {
      Finish({}, "Print manager unavailable.");
      return;
    }

    content::RenderFrameHost* rfh = web_contents_->GetPrimaryMainFrame();
    std::variant<printing::mojom::PrintPagesParamsPtr, std::string>
        print_pages_params = print_to_pdf::GetPrintPagesParams(
            rfh->GetLastCommittedURL(), /*landscape=*/std::nullopt,
            /*display_header_footer=*/std::nullopt,
            /*print_background=*/true, /*scale=*/std::nullopt,
            /*paper_width=*/std::nullopt, /*paper_height=*/std::nullopt,
            /*margin_top=*/std::nullopt, /*margin_bottom=*/std::nullopt,
            /*margin_left=*/std::nullopt, /*margin_right=*/std::nullopt,
            /*header_template=*/std::nullopt,
            /*footer_template=*/std::nullopt,
            /*prefer_css_page_size=*/std::nullopt,
            /*generate_tagged_pdf=*/std::nullopt,
            /*generate_document_outline=*/std::nullopt);
    if (std::holds_alternative<std::string>(print_pages_params)) {
      Finish({}, std::get<std::string>(print_pages_params));
      return;
    }

    print_manager->PrintToPdf(
        rfh, /*page_ranges=*/std::string(),
        std::move(std::get<printing::mojom::PrintPagesParamsPtr>(
            print_pages_params)),
        base::BindOnce(&PdfDocumentGenerator::OnPdfPrinted,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    Finish({}, "Renderer process gone while rendering HTML for PDF.");
  }

  void OnPdfPrinted(print_to_pdf::PdfPrintResult result,
                    scoped_refptr<base::RefCountedMemory> data) {
    if (result != print_to_pdf::PdfPrintResult::kPrintSuccess || !data) {
      Finish({}, "Failed to render the PDF.");
      return;
    }
    Finish(std::vector<uint8_t>(data->begin(), data->end()), "");
  }

  void OnTimeout() { Finish({}, "Timed out generating the PDF."); }

  void Finish(std::vector<uint8_t> pdf_bytes, std::string error_message) {
    timeout_timer_.Stop();
    weak_ptr_factory_.InvalidateWeakPtrs();
    if (web_contents_) {
      Observe(nullptr);
      web_contents_->SetDelegate(nullptr);
      web_contents_.reset();
    }
    if (!temp_html_path_.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce(&DeleteTempFile, std::move(temp_html_path_)));
    }
    if (callback_) {
      std::move(callback_).Run(std::move(pdf_bytes),
                               std::move(error_message));
    }
    delete this;
  }

  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  std::unique_ptr<content::WebContents> web_contents_;
  base::FilePath temp_html_path_;
  base::OneShotTimer timeout_timer_;
  GenerateCallback callback_;
  base::WeakPtrFactory<PdfDocumentGenerator> weak_ptr_factory_{this};
};

}  // namespace

CreatePdfDocumentTool::CreatePdfDocumentTool(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {}

CreatePdfDocumentTool::~CreatePdfDocumentTool() = default;

std::string_view CreatePdfDocumentTool::Name() const {
  return mojom::kCreatePdfDocumentToolName;
}

std::string_view CreatePdfDocumentTool::Description() const {
  return "Create a PDF from HTML content and download it to the user's "
         "device. Unlike the Word/Excel/PowerPoint tools, this renders "
         "real HTML/CSS (headings, tables, styling), so prefer it when "
         "rich formatting matters. Provide a complete HTML document (or a "
         "fragment - it will be wrapped automatically).";
}

std::optional<base::DictValue> CreatePdfDocumentTool::InputProperties()
    const {
  return CreateInputProperties(
      {{kPropertyNameFilename,
        StringProperty("The filename to save as, without extension (the "
                       ".pdf extension is added automatically).")},
       {kPropertyNameHtmlContent,
        StringProperty("The HTML content to render into the PDF.")}});
}

std::optional<std::vector<std::string>>
CreatePdfDocumentTool::RequiredProperties() const {
  return std::vector<std::string>{kPropertyNameFilename,
                                  kPropertyNameHtmlContent};
}

void CreatePdfDocumentTool::UseTool(const std::string& input_json,
                                    UseToolCallback callback) {
  auto input = base::JSONReader::ReadDict(input_json,
                                          base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!input.has_value()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: failed to parse input JSON"), {});
    return;
  }

  const std::string* filename_value = input->FindString(kPropertyNameFilename);
  if (!filename_value || filename_value->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText("Error: missing or empty 'filename'"), {});
    return;
  }

  const std::string* html_content =
      input->FindString(kPropertyNameHtmlContent);
  if (!html_content || html_content->empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            "Error: missing or empty 'html_content'"),
        {});
    return;
  }

  std::string filename = base::StrCat({*filename_value, ".pdf"});

  // Self-deletes once its callback runs - see PdfDocumentGenerator's class
  // comment.
  auto* generator = new PdfDocumentGenerator();
  generator->Generate(
      browser_context_, *html_content,
      base::BindOnce(&CreatePdfDocumentTool::OnPdfGenerated,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void CreatePdfDocumentTool::OnPdfGenerated(UseToolCallback callback,
                                           std::string filename,
                                           std::vector<uint8_t> pdf_bytes,
                                           std::string error_message) {
  if (pdf_bytes.empty()) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: failed to create '", filename,
                          "': ", error_message})),
        {});
    return;
  }

  DownloadGeneratedBytes(
      browser_context_, filename, std::move(pdf_bytes),
      base::BindOnce(&CreatePdfDocumentTool::OnDownloadComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     filename));
}

void CreatePdfDocumentTool::OnDownloadComplete(UseToolCallback callback,
                                               std::string filename,
                                               DocumentDownloadResult result) {
  if (!result.success) {
    std::move(callback).Run(
        CreateContentBlocksForText(
            base::StrCat({"Error: failed to create '", filename,
                          "': ", result.error_message})),
        {});
    return;
  }
  std::move(callback).Run(
      CreateContentBlocksForText(
          base::StrCat({"Created and started downloading '", filename, "'."})),
      {});
}

}  // namespace ai_chat

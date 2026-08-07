// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/document_download_util.h"

#include <memory>
#include <set>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/download/public/common/download_url_parameters.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "third_party/re2/src/re2/re2.h"
#include "third_party/zlib/google/zip.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace ai_chat {

std::string XmlEscape(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += c;
    }
  }
  return escaped;
}

namespace {

// Writes `file_bytes` to a new temp file and returns its path, or an empty
// path on failure. Runs on a background sequence.
base::FilePath WriteBytesToTempFile(std::vector<uint8_t> file_bytes) {
  base::FilePath temp_path;
  if (!base::CreateTemporaryFile(&temp_path)) {
    return base::FilePath();
  }
  if (!base::WriteFile(temp_path, file_bytes)) {
    base::DeleteFile(temp_path);
    return base::FilePath();
  }
  return temp_path;
}

// Writes each of `parts` into a fresh temp directory, zips it into a new
// temp file, and returns the zip's path (or an empty path on failure). Runs
// on a background sequence.
base::FilePath BuildOoxmlArchive(std::vector<OoxmlPart> parts) {
  base::ScopedTempDir parts_dir;
  if (!parts_dir.CreateUniqueTempDir()) {
    return base::FilePath();
  }

  for (const auto& part : parts) {
    base::FilePath part_path = parts_dir.GetPath().AppendASCII(part.path);
    if (!base::CreateDirectory(part_path.DirName())) {
      return base::FilePath();
    }
    if (!base::WriteFile(part_path, part.content)) {
      return base::FilePath();
    }
  }

  base::FilePath archive_path;
  if (!base::CreateTemporaryFile(&archive_path)) {
    return base::FilePath();
  }
  // The OOXML root relationships part is literally named ".rels" - a
  // dot-prefixed name that zip::Zip treats as "hidden" and otherwise
  // silently omits, producing an archive Word considers corrupt.
  if (!zip::Zip(parts_dir.GetPath(), archive_path,
                /*include_hidden_files=*/true)) {
    base::DeleteFile(archive_path);
    return base::FilePath();
  }
  return archive_path;
}

void StartDownload(content::BrowserContext* browser_context,
                    std::string filename,
                    DocumentDownloadCallback callback,
                    base::FilePath generated_file_path) {
  if (generated_file_path.empty()) {
    std::move(callback).Run(
        {.success = false,
         .error_message = "Failed to generate the file on disk."});
    return;
  }

  static const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("ai_chat_generated_document", R"(
        semantics {
          sender: "AI Chat Document Generation Tool"
          description:
            "Downloads a file (Word/Excel/PowerPoint document or PDF) that "
            "the AI Chat assistant generated locally at the user's request. "
            "The file never leaves the device - this annotation covers "
            "only handing the already-generated local file to the "
            "browser's normal download pipeline so it appears in the "
            "user's Downloads."
          trigger: "User asks the AI Chat assistant to create a document."
          data: "None - the source is a local file, not a network request."
          destination: LOCAL
          internal {
            contacts {
              email: "ai-chat@brave.com"
            }
          }
          user_data {
            type: NONE
          }
          last_reviewed: "2026-08-04"
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled independently of AI Chat."
          policy_exception_justification:
            "Not a network request; downloads a locally-generated file."
        })");

  auto params = std::make_unique<download::DownloadUrlParameters>(
      net::FilePathToFileURL(generated_file_path), kTrafficAnnotation);
  params->set_suggested_name(base::UTF8ToUTF16(filename));
  params->set_download_source(download::DownloadSource::WEB_CONTENTS_API);

  browser_context->GetDownloadManager()->DownloadUrl(std::move(params));
  std::move(callback).Run({.success = true});
}

// Presents a native "Save As" dialog anchored to a WebContents, then reports
// back which path (if any) the user chose. Self-deleting, mirroring the
// established pattern in chrome/browser/devtools/devtools_select_file_dialog.cc.
class SaveAsDialog : public ui::SelectFileDialog::Listener {
 public:
  using PathCallback = base::OnceCallback<void(base::FilePath)>;

  static void Show(content::WebContents* web_contents,
                   const base::FilePath& default_path,
                   PathCallback callback) {
    new SaveAsDialog(web_contents, default_path, std::move(callback));
  }

  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    std::move(callback_).Run(file.path());
    delete this;
  }

  void FileSelectionCanceled() override {
    std::move(callback_).Run(base::FilePath());
    delete this;
  }

 private:
  SaveAsDialog(content::WebContents* web_contents,
              const base::FilePath& default_path,
              PathCallback callback)
      : callback_(std::move(callback)),
        dialog_(ui::SelectFileDialog::Create(
            this,
            std::make_unique<ChromeSelectFilePolicy>(web_contents))) {
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    base::FilePath::StringType extension;
    if (default_path.Extension().size() > 0) {
      extension = default_path.Extension().substr(1);
      file_type_info.extensions.resize(1);
      file_type_info.extensions[0].push_back(extension);
    }
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(),
        default_path, &file_type_info, 0, extension,
        platform_util::GetTopLevel(web_contents->GetNativeView()));
  }

  ~SaveAsDialog() override { dialog_->ListenerDestroyed(); }

  PathCallback callback_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

// Presents a native "Open" dialog anchored to a WebContents, restricted to
// .docx files, then reports back which path (if any) the user chose.
// Self-deleting, mirroring SaveAsDialog above.
class OpenDocxDialog : public ui::SelectFileDialog::Listener {
 public:
  using PathCallback = base::OnceCallback<void(base::FilePath)>;

  static void Show(content::WebContents* web_contents,
                   PathCallback callback) {
    new OpenDocxDialog(web_contents, std::move(callback));
  }

  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    std::move(callback_).Run(file.path());
    delete this;
  }

  void FileSelectionCanceled() override {
    std::move(callback_).Run(base::FilePath());
    delete this;
  }

 private:
  explicit OpenDocxDialog(content::WebContents* web_contents,
                          PathCallback callback)
      : callback_(std::move(callback)),
        dialog_(ui::SelectFileDialog::Create(
            this,
            std::make_unique<ChromeSelectFilePolicy>(web_contents))) {
    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions.resize(1);
    file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("docx"));
    dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
        base::FilePath(), &file_type_info, 0, FILE_PATH_LITERAL("docx"),
        platform_util::GetTopLevel(web_contents->GetNativeView()));
  }

  ~OpenDocxDialog() override { dialog_->ListenerDestroyed(); }

  PathCallback callback_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

// Runs on a background sequence. Unzips `docx_path` and pulls out the plain
// text of each <w:p>...</w:p> paragraph's <w:t> runs (concatenated without a
// separator, since Word often splits a single word or sentence across
// several runs), joined with a newline between paragraphs - matches the
// paragraph/run shape this file's own BuildWordDocumentXml produces, and is
// the shape most real-world .docx files use for plain, unstyled text.
std::optional<std::string> ReadWordDocumentTextOnBackgroundSequence(
    base::FilePath docx_path) {
  base::ScopedTempDir extract_dir;
  if (!extract_dir.CreateUniqueTempDir()) {
    return std::nullopt;
  }
  if (!zip::Unzip(docx_path, extract_dir.GetPath())) {
    return std::nullopt;
  }
  std::string xml;
  if (!base::ReadFileToString(
          extract_dir.GetPath().AppendASCII("word").AppendASCII(
              "document.xml"),
          &xml)) {
    return std::nullopt;
  }

  static const base::NoDestructor<re2::RE2> kParagraphRe(
      R"((?s)<w:p[ >].*?</w:p>)");
  static const base::NoDestructor<re2::RE2> kTextRunRe(
      R"(<w:t[^>]*>([^<]*)</w:t>)");

  std::vector<std::string> paragraphs;
  re2::StringPiece document_input(xml);
  std::string paragraph_xml;
  while (RE2::FindAndConsume(&document_input, *kParagraphRe, &paragraph_xml)) {
    std::string paragraph_text;
    re2::StringPiece paragraph_input(paragraph_xml);
    std::string run_text;
    while (RE2::FindAndConsume(&paragraph_input, *kTextRunRe, &run_text)) {
      paragraph_text += run_text;
    }
    paragraphs.push_back(std::move(paragraph_text));
  }
  return base::JoinString(paragraphs, "\n");
}

void OnDocxPathChosenForReading(
    base::OnceCallback<void(std::optional<std::string>)> callback,
    base::FilePath chosen_path) {
  if (chosen_path.empty()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ReadWordDocumentTextOnBackgroundSequence,
                     std::move(chosen_path)),
      std::move(callback));
}

// Runs on a background sequence.
DocumentDownloadResult MoveGeneratedFileTo(base::FilePath from,
                                           base::FilePath to) {
  if (!base::Move(from, to)) {
    return {.success = false,
            .error_message = "Could not write the file to that location."};
  }
  return {.success = true};
}

void OnSaveAsPathChosen(base::FilePath generated_file_path,
                        DocumentDownloadCallback callback,
                        base::FilePath chosen_path) {
  if (chosen_path.empty()) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::GetDeleteFileCallback(generated_file_path));
    std::move(callback).Run(
        {.success = false, .error_message = "Save canceled."});
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&MoveGeneratedFileTo, std::move(generated_file_path),
                      std::move(chosen_path)),
      std::move(callback));
}

void ShowSaveAsDialogForGeneratedFile(
    base::WeakPtr<content::WebContents> web_contents,
    std::string default_filename,
    DocumentDownloadCallback callback,
    base::FilePath generated_file_path) {
  if (generated_file_path.empty()) {
    std::move(callback).Run(
        {.success = false,
         .error_message = "Failed to generate the file on disk."});
    return;
  }
  if (!web_contents) {
    base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                               base::GetDeleteFileCallback(generated_file_path));
    std::move(callback).Run(
        {.success = false, .error_message = "The panel was closed."});
    return;
  }

  base::FilePath default_path =
      DownloadPrefs::FromBrowserContext(web_contents->GetBrowserContext())
          ->DownloadPath()
          .AppendASCII(default_filename);

  SaveAsDialog::Show(
      web_contents.get(), default_path,
      base::BindOnce(&OnSaveAsPathChosen, std::move(generated_file_path),
                     std::move(callback)));
}

}  // namespace

void BuildOoxmlArchiveAndDownload(content::BrowserContext* browser_context,
                                   const std::string& filename,
                                   std::vector<OoxmlPart> parts,
                                   DocumentDownloadCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&BuildOoxmlArchive, std::move(parts)),
      base::BindOnce(&StartDownload, browser_context, filename,
                      std::move(callback)));
}

void BuildOoxmlArchiveAndSaveAs(content::WebContents* web_contents,
                                const std::string& default_filename,
                                std::vector<OoxmlPart> parts,
                                DocumentDownloadCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&BuildOoxmlArchive, std::move(parts)),
      base::BindOnce(&ShowSaveAsDialogForGeneratedFile,
                     web_contents->GetWeakPtr(), default_filename,
                     std::move(callback)));
}

void ShowOpenDialogAndReadWordDocumentText(
    content::WebContents* web_contents,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  OpenDocxDialog::Show(
      web_contents,
      base::BindOnce(&OnDocxPathChosenForReading, std::move(callback)));
}

void DownloadGeneratedBytes(content::BrowserContext* browser_context,
                             const std::string& filename,
                             std::vector<uint8_t> file_bytes,
                             DocumentDownloadCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&WriteBytesToTempFile, std::move(file_bytes)),
      base::BindOnce(&StartDownload, browser_context, filename,
                      std::move(callback)));
}

const char kWordContentTypesXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "content-types\">"
    "<Default Extension=\"rels\" "
    "ContentType=\"application/vnd.openxmlformats-package.relationships+"
    "xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument."
    "wordprocessingml.document.main+xml\"/>"
    "</Types>";

const char kWordRootRelsXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
    "relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
    "relationships/officeDocument\" Target=\"word/document.xml\"/>"
    "</Relationships>";

namespace {

// Returns the <w:rPr> run-formatting fragment for a given heading level
// (0 = normal body text). Uses direct formatting (bold + larger size)
// rather than named styles, so no word/styles.xml part is needed - keeps
// the archive minimal while still visibly distinguishing headings.
std::string RunPropertiesForHeadingLevel(int heading_level) {
  if (heading_level <= 0) {
    return "";
  }
  // Heading 1 -> 32pt, Heading 2 -> 28pt, Heading 3+ -> 24pt. Word sizes
  // ("w:sz") are in half-points.
  int half_points = heading_level == 1 ? 64 : heading_level == 2 ? 56 : 48;
  return base::StrCat({"<w:rPr><w:b/><w:sz w:val=\"",
                        base::NumberToString(half_points), "\"/></w:rPr>"});
}

// Minimal DrawingML for one inline image referencing `relationship_id` (an
// id in word/_rels/document.xml.rels, see BuildDocumentRelsXml). All the
// drawingml/picture namespaces are declared locally on this element rather
// than on the document root, so this doesn't affect documents with no
// images.
std::string BuildImageDrawingParagraphXml(const std::string& relationship_id,
                                          int width_emu,
                                          int height_emu,
                                          int drawing_id) {
  std::string id_str = base::NumberToString(drawing_id);
  std::string extent = base::StrCat({"cx=\"", base::NumberToString(width_emu),
                                     "\" cy=\"",
                                     base::NumberToString(height_emu), "\""});
  return base::StrCat(
      {"<w:p><w:r><w:drawing>"
       "<wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/"
       "2006/wordprocessingDrawing\">"
       "<wp:extent ", extent, "/>"
       "<wp:docPr id=\"", id_str, "\" name=\"Picture ", id_str, "\"/>"
       "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/"
       "2006/main\">"
       "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/"
       "2006/picture\">"
       "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/"
       "2006/picture\">"
       "<pic:nvPicPr><pic:cNvPr id=\"", id_str, "\" name=\"Picture ", id_str,
       "\"/><pic:cNvPicPr/></pic:nvPicPr>"
       "<pic:blipFill><a:blip "
       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
       "relationships\" r:embed=\"", relationship_id, "\"/>"
       "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
       "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext ", extent,
       "/></a:xfrm>"
       "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
       "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing>"
       "</w:r></w:p>"});
}

}  // namespace

std::string BuildContentTypesXmlWithImages(
    const std::vector<EmbeddedImage>& images) {
  std::set<std::string> extensions_seen;
  std::string extra;
  for (const auto& image : images) {
    if (extensions_seen.insert(image.extension).second) {
      base::StrAppend(&extra, {"<Default Extension=\"", image.extension,
                               "\" ContentType=\"", image.content_type,
                               "\"/>"});
    }
  }
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/"
       "content-types\">"
       "<Default Extension=\"rels\" "
       "ContentType=\"application/vnd.openxmlformats-package.relationships+"
       "xml\"/>"
       "<Default Extension=\"xml\" ContentType=\"application/xml\"/>",
       extra,
       "<Override PartName=\"/word/document.xml\" "
       "ContentType=\"application/vnd.openxmlformats-officedocument."
       "wordprocessingml.document.main+xml\"/>"
       "</Types>"});
}

std::string BuildDocumentRelsXml(const std::vector<EmbeddedImage>& images) {
  std::string body;
  for (const auto& image : images) {
    base::StrAppend(
        &body,
        {"<Relationship Id=\"", image.relationship_id,
         "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
         "relationships/image\" Target=\"", image.media_path, "\"/>"});
  }
  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/"
       "2006/relationships\">",
       body, "</Relationships>"});
}

std::string BuildWordDocumentXml(const base::ListValue& paragraphs) {
  constexpr char kParagraphPropertyText[] = "text";
  constexpr char kParagraphPropertyHeadingLevel[] = "heading_level";
  constexpr char kParagraphPropertyImageRelId[] = "image_relationship_id";
  constexpr char kParagraphPropertyImageWidthEmu[] = "image_width_emu";
  constexpr char kParagraphPropertyImageHeightEmu[] = "image_height_emu";

  std::string body;
  int drawing_id = 1;
  for (const auto& paragraph_value : paragraphs) {
    const base::DictValue* paragraph_dict = paragraph_value.GetIfDict();
    if (!paragraph_dict) {
      continue;
    }

    if (const std::string* relationship_id =
            paragraph_dict->FindString(kParagraphPropertyImageRelId)) {
      int width_emu =
          paragraph_dict->FindInt(kParagraphPropertyImageWidthEmu)
              .value_or(3657600);
      int height_emu =
          paragraph_dict->FindInt(kParagraphPropertyImageHeightEmu)
              .value_or(2743200);
      base::StrAppend(&body, {BuildImageDrawingParagraphXml(
                                 *relationship_id, width_emu, height_emu,
                                 drawing_id++)});
      continue;
    }

    const std::string* text =
        paragraph_dict->FindString(kParagraphPropertyText);
    if (!text) {
      continue;
    }
    int heading_level =
        paragraph_dict->FindInt(kParagraphPropertyHeadingLevel).value_or(0);
    std::string run_properties = RunPropertiesForHeadingLevel(heading_level);

    // Multi-point text (e.g. a model's list-style answer) arrives as
    // newline-separated lines, but WordprocessingML treats a literal "\n"
    // inside a run as insignificant whitespace - without splitting these
    // into their own paragraphs, every line collapses into one run-on
    // paragraph. A leading "- " becomes a real bullet glyph with a hanging
    // indent instead of just a dash character.
    for (const auto& line :
         base::SplitStringPiece(*text, "\n", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      const bool is_bullet = base::StartsWith(line, "- ");
      const std::string_view line_text = is_bullet ? line.substr(2) : line;
      const char* paragraph_properties =
          is_bullet ? "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr>"
                    : "";
      base::StrAppend(
          &body,
          {"<w:p>", paragraph_properties, "<w:r>", run_properties,
           "<w:t xml:space=\"preserve\">", is_bullet ? "• " : "",
           XmlEscape(std::string(line_text)), "</w:t></w:r></w:p>"});
    }
  }

  return base::StrCat(
      {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
       "<w:document "
       "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
       "main\"><w:body>",
       body, "<w:sectPr/></w:body></w:document>"});
}

}  // namespace ai_chat

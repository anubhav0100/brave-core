// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ai_chat/tools/image_embed_util.h"

#include <optional>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace ai_chat {

EmbeddedImage::EmbeddedImage() = default;
EmbeddedImage::EmbeddedImage(EmbeddedImage&&) = default;
EmbeddedImage& EmbeddedImage::operator=(EmbeddedImage&&) = default;
EmbeddedImage::~EmbeddedImage() = default;

namespace {

constexpr size_t kMaxImages = 12;
// SimpleURLLoader::DownloadToString DCHECKs (fatally) if asked for more than
// this, so it's a hard ceiling, not just a size preference.
constexpr size_t kMaxImageBytes =
    network::SimpleURLLoader::kMaxBoundedStringDownloadSize;

// Returns {extension, content_type} for a recognized raster image format
// sniffed from its magic bytes, or nullopt if unrecognized.
std::optional<std::pair<std::string, std::string>> SniffImageFormat(
    const std::string& bytes) {
  if (bytes.size() >= 8 &&
      bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) {
    return std::make_pair("png", "image/png");
  }
  if (bytes.size() >= 3 && static_cast<uint8_t>(bytes[0]) == 0xFF &&
      static_cast<uint8_t>(bytes[1]) == 0xD8 &&
      static_cast<uint8_t>(bytes[2]) == 0xFF) {
    return std::make_pair("jpeg", "image/jpeg");
  }
  if (bytes.size() >= 6 && bytes.compare(0, 3, "GIF", 3) == 0) {
    return std::make_pair("gif", "image/gif");
  }
  if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF", 4) == 0 &&
      bytes.compare(8, 4, "WEBP", 4) == 0) {
    return std::make_pair("webp", "image/webp");
  }
  if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
    return std::make_pair("bmp", "image/bmp");
  }
  return std::nullopt;
}

// PNG: after the 8-byte signature, the IHDR chunk is always first - 4 bytes
// length, 4 bytes "IHDR", then 4 bytes width + 4 bytes height, both
// big-endian.
void TryParsePngDimensions(const std::string& bytes, int* width, int* height) {
  if (bytes.size() < 24) {
    return;
  }
  auto read_be32 = [&](size_t offset) {
    return (static_cast<uint8_t>(bytes[offset]) << 24) |
           (static_cast<uint8_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint8_t>(bytes[offset + 2]) << 8) |
           static_cast<uint8_t>(bytes[offset + 3]);
  };
  *width = read_be32(16);
  *height = read_be32(20);
}

// JPEG: walk the marker segments after the SOI (0xFFD8) looking for a Start
// Of Frame marker (0xC0-0xC3 cover baseline/extended-sequential/
// progressive, by far the common cases) - its payload is 1 byte precision,
// then height and width as big-endian uint16s.
void TryParseJpegDimensions(const std::string& bytes,
                            int* width,
                            int* height) {
  size_t pos = 2;  // Skip SOI.
  while (pos + 4 <= bytes.size()) {
    if (static_cast<uint8_t>(bytes[pos]) != 0xFF) {
      ++pos;
      continue;
    }
    uint8_t marker = static_cast<uint8_t>(bytes[pos + 1]);
    // Markers with no payload/length.
    if (marker == 0xD8 || marker == 0xD9 ||
        (marker >= 0xD0 && marker <= 0xD7)) {
      pos += 2;
      continue;
    }
    if (pos + 4 > bytes.size()) {
      break;
    }
    size_t segment_length = (static_cast<uint8_t>(bytes[pos + 2]) << 8) |
                            static_cast<uint8_t>(bytes[pos + 3]);
    bool is_sof = marker >= 0xC0 && marker <= 0xC3;
    if (is_sof && pos + 4 + 5 <= bytes.size()) {
      size_t payload = pos + 4;
      *height = (static_cast<uint8_t>(bytes[payload + 1]) << 8) |
                static_cast<uint8_t>(bytes[payload + 2]);
      *width = (static_cast<uint8_t>(bytes[payload + 3]) << 8) |
               static_cast<uint8_t>(bytes[payload + 4]);
      return;
    }
    if (marker == 0xDA) {
      break;  // Start of Scan - no SOF found before image data began.
    }
    pos += 2 + segment_length;
  }
}

// Owns one batch of in-flight image downloads. Self-deleting - lives only
// for the duration of the batch.
class ImageBatchFetcher {
 public:
  static void Start(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::vector<GURL> image_urls,
      FetchImagesForEmbeddingCallback callback) {
    new ImageBatchFetcher(std::move(url_loader_factory),
                          std::move(image_urls), std::move(callback));
  }

 private:
  ImageBatchFetcher(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::vector<GURL> image_urls,
      FetchImagesForEmbeddingCallback callback)
      : url_loader_factory_(std::move(url_loader_factory)),
        callback_(std::move(callback)) {
    std::vector<GURL> filtered;
    for (auto& url : image_urls) {
      if (filtered.size() >= kMaxImages) {
        break;
      }
      if (!url.is_valid() || url.SchemeIs("data")) {
        continue;
      }
      filtered.push_back(std::move(url));
    }

    if (filtered.empty()) {
      std::move(callback_).Run({});
      delete this;
      return;
    }

    results_.resize(filtered.size());
    base::RepeatingClosure barrier = base::BarrierClosure(
        filtered.size(), base::BindOnce(&ImageBatchFetcher::OnAllDone,
                                        weak_factory_.GetWeakPtr()));
    static const net::NetworkTrafficAnnotationTag kTrafficAnnotation =
        net::DefineNetworkTrafficAnnotation("ai_chat_page_capture_image", R"(
          semantics {
            sender: "AI Chat Page Capture Tool"
            description:
              "Downloads an image that was visible on a page the user asked "
              "the AI Assistant to capture, so it can be embedded into the "
              "generated Word document instead of just a link."
            trigger:
              "User asks the AI Assistant to capture a page's content that "
              "includes images."
            data: "None - a GET request to the image's own URL."
            destination: WEBSITE
            internal {
              contacts {
                email: "ai-chat@brave.com"
              }
            }
            user_data {
              type: NONE
            }
            last_reviewed: "2026-08-07"
          }
          policy {
            cookies_allowed: NO
            setting: "This feature cannot be disabled independently of AI Chat."
            policy_exception_justification:
              "Only fetches images already visible on a page the user "
              "explicitly asked to capture."
          })");
    for (size_t i = 0; i < filtered.size(); ++i) {
      auto request = std::make_unique<network::ResourceRequest>();
      request->url = filtered[i];
      request->method = "GET";
      request->credentials_mode = network::mojom::CredentialsMode::kOmit;
      auto loader =
          network::SimpleURLLoader::Create(std::move(request),
                                           kTrafficAnnotation);
      loader->SetAllowHttpErrorResults(false);
      network::SimpleURLLoader* loader_ptr = loader.get();
      loaders_.push_back(std::move(loader));
      loader_ptr->DownloadToString(
          url_loader_factory_.get(),
          base::BindOnce(&ImageBatchFetcher::OnDownloaded,
                         weak_factory_.GetWeakPtr(), i, filtered[i], barrier),
          kMaxImageBytes);
    }
  }

  void OnDownloaded(size_t index,
                    GURL url,
                    base::RepeatingClosure barrier,
                    std::optional<std::string> body) {
    if (body && !body->empty()) {
      auto format = SniffImageFormat(*body);
      if (format) {
        EmbeddedImage image;
        image.source_url = url;
        image.extension = format->first;
        image.content_type = format->second;
        image.bytes.assign(body->begin(), body->end());
        if (image.extension == "png") {
          TryParsePngDimensions(*body, &image.width_px, &image.height_px);
        } else if (image.extension == "jpeg") {
          TryParseJpegDimensions(*body, &image.width_px, &image.height_px);
        }
        results_[index] = std::move(image);
      }
    }
    barrier.Run();
  }

  void OnAllDone() {
    std::vector<EmbeddedImage> embedded;
    int next_id = 2;  // rId1 is conventionally reserved elsewhere.
    for (size_t i = 0; i < results_.size(); ++i) {
      if (results_[i].bytes.empty()) {
        continue;
      }
      EmbeddedImage image = std::move(results_[i]);
      image.relationship_id = base::StrCat({"rId", base::NumberToString(next_id)});
      image.media_path =
          base::StrCat({"media/image", base::NumberToString(next_id),
                        ".", image.extension});
      ++next_id;
      embedded.push_back(std::move(image));
    }
    std::move(callback_).Run(std::move(embedded));
    delete this;
  }

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  FetchImagesForEmbeddingCallback callback_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::vector<EmbeddedImage> results_;
  base::WeakPtrFactory<ImageBatchFetcher> weak_factory_{this};
};

}  // namespace

void FetchImagesForEmbedding(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::vector<GURL> image_urls,
    FetchImagesForEmbeddingCallback callback) {
  ImageBatchFetcher::Start(std::move(url_loader_factory),
                           std::move(image_urls), std::move(callback));
}

}  // namespace ai_chat

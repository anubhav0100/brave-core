// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_AI_CHAT_TOOLS_IMAGE_EMBED_UTIL_H_
#define BRAVE_BROWSER_AI_CHAT_TOOLS_IMAGE_EMBED_UTIL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace ai_chat {

// One image successfully downloaded and ready to embed in a generated
// OOXML document - assigned a relationship id and archive-relative media
// path by FetchImagesForEmbedding, in the order they should be embedded.
struct EmbeddedImage {
  EmbeddedImage();
  EmbeddedImage(EmbeddedImage&&);
  EmbeddedImage& operator=(EmbeddedImage&&);
  ~EmbeddedImage();

  // e.g. "rId2" - unique within the document part's own relationships file.
  std::string relationship_id;
  // Archive path relative to word/, e.g. "media/image1.png".
  std::string media_path;
  // e.g. "png", "jpeg" - used for both the media filename and the
  // [Content_Types].xml Default Extension entry.
  std::string extension;
  // e.g. "image/png".
  std::string content_type;
  std::vector<uint8_t> bytes;
  // 0 if dimensions couldn't be determined (only PNG and JPEG are parsed) -
  // callers should fall back to a fixed display size in that case.
  int width_px = 0;
  int height_px = 0;
};

using FetchImagesForEmbeddingCallback =
    base::OnceCallback<void(std::vector<EmbeddedImage>)>;

// Downloads each of `image_urls`, in order, up to a bounded count/size (to
// keep generated documents a reasonable size and avoid downloading a page's
// entire image gallery) - currently 12 images, 6MB each. Images that fail to
// download, exceed the size cap, aren't a recognized raster format, or use
// a "data:" URL (already-local, not worth a network round trip - callers
// wanting those should decode them directly) are silently skipped. Pixel
// dimensions are only detected for PNG and JPEG; other accepted formats
// (GIF, WEBP, BMP) are still embedded but with width_px/height_px left 0.
// `callback` always receives whatever subset succeeded, even if that's
// empty - this never reports a hard failure for the overall document.
void FetchImagesForEmbedding(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::vector<GURL> image_urls,
    FetchImagesForEmbeddingCallback callback);

}  // namespace ai_chat

#endif  // BRAVE_BROWSER_AI_CHAT_TOOLS_IMAGE_EMBED_UTIL_H_

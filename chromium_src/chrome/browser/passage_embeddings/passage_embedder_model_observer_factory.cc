/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

// Gate the passage embedder model observer on the per-profile history
// embeddings toggle OR the AI Chat content-indexing toggle. Both embedding
// services (PageEmbeddingsService, HistoryEmbeddingsService) refuse to build
// without this observer, so this keeps the on-device AI embedder from being
// created unless at least one consumer wants it. Applied at service
// creation, so a toggle change takes effect on restart.
//
// The content-indexing pref is read by its raw name rather than through
// brave/browser/ai_chat/content_index/ai_chat_content_index.h - that target
// depends on brave/browser/history_embeddings, which is a public_dep of
// //chrome/browser/passage_embeddings (this target), so including it here
// would create a GN dependency cycle. Keep this string in sync with
// kContentIndexingEnabledPref in ai_chat_content_index.cc.

#include "chrome/browser/history_embeddings/history_embeddings_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"

namespace history_embeddings {

// Per-profile overload the macro below routes the upstream no-arg call to,
// using the `context` in scope at the call site.
bool IsHistoryEmbeddingsFeatureEnabled(content::BrowserContext* context) {
  Profile* profile = Profile::FromBrowserContext(context);
  return IsHistoryEmbeddingsEnabledForProfile(profile) ||
         profile->GetPrefs()->GetBoolean(
             "brave.ai_chat.content_indexing_enabled");
}

}  // namespace history_embeddings

#define IsHistoryEmbeddingsFeatureEnabled() \
  IsHistoryEmbeddingsFeatureEnabled(context)

#include <chrome/browser/passage_embeddings/passage_embedder_model_observer_factory.cc>

#undef IsHistoryEmbeddingsFeatureEnabled

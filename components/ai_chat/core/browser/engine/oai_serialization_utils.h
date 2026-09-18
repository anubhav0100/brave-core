// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_ENGINE_OAI_SERIALIZATION_UTILS_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_ENGINE_OAI_SERIALIZATION_UTILS_H_

// Shared utilities for serializing OAI content block types (mojom) to
// base::DictValue.

#include "base/values.h"
#include "brave/components/ai_chat/core/browser/engine/oai_message_utils.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom-forward.h"

namespace ai_chat {

// Converts a MemoryContentBlock's memory map to a base::DictValue.
// String values are stored directly; list values become base::ListValue.
base::DictValue MemoryContentBlockToDict(
    const mojom::MemoryContentBlock& block);

// Converts a FileContentBlock to {"filename": ..., "file_data": ...}.
base::DictValue FileContentBlockToDict(const mojom::FileContentBlock& block);

// Converts an ImageContentBlock to {"url": ...}.
base::DictValue ImageContentBlockToDict(const mojom::ImageContentBlock& block);

// Serializes tool_calls and tool_call_id from an OAIMessage onto message_dict.
void SerializeToolCallsOnMessageDict(const OAIMessage& message,
                                     base::DictValue& message_dict);

// Converts an already-built Chat Completions-shaped message list (as
// produced by OAIAPIClient::SerializeOAIMessages - each dict has "role",
// "content" as an array of {"type":"text"/"image_url", ...} blocks, and
// optionally "tool_calls"/"tool_call_id") into OpenAI's Responses API
// "input" array shape instead: "system" becomes a "developer"-role
// message, a "tool" role message becomes a role-less
// {"type":"function_call_output", "call_id", "output"} item, and an
// assistant message's tool_calls each become their own
// {"type":"function_call", "call_id", "name", "arguments"} item rather
// than living inside a message. Reuses the existing content-block
// serialization instead of duplicating it, so only the outer envelope is
// remapped here. Only "text" and "image_url" content blocks are
// supported for this path currently - others are silently dropped.
base::ListValue ConvertOAIMessagesToResponsesApiInput(
    const base::ListValue& chat_messages);

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_ENGINE_OAI_SERIALIZATION_UTILS_H_

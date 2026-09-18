// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/engine/oai_serialization_utils.h"

#include <utility>

#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom.h"

namespace ai_chat {

namespace {

// Extracts and concatenates the text of every "text"-type content block -
// used for a tool-result message's "output" field, which the Responses
// API expects as a plain string rather than a content-block array.
std::string FlattenTextContentBlocks(const base::ListValue* content) {
  std::string text;
  if (!content) {
    return text;
  }
  for (const auto& block_value : *content) {
    if (!block_value.is_dict()) {
      continue;
    }
    const base::DictValue& block = block_value.GetDict();
    const std::string* type = block.FindString("type");
    if (!type || *type != "text") {
      continue;
    }
    if (const std::string* block_text = block.FindString("text")) {
      text += *block_text;
    }
  }
  return text;
}

}  // namespace

base::DictValue MemoryContentBlockToDict(
    const mojom::MemoryContentBlock& block) {
  base::DictValue memory_dict;
  for (const auto& [key, memory_value] : block.memory) {
    if (memory_value->is_string_value()) {
      memory_dict.Set(key, memory_value->get_string_value());
    } else if (memory_value->is_list_value()) {
      base::ListValue list;
      for (const auto& val : memory_value->get_list_value()) {
        list.Append(val);
      }
      memory_dict.Set(key, std::move(list));
    }
  }
  return memory_dict;
}

base::DictValue FileContentBlockToDict(const mojom::FileContentBlock& block) {
  base::DictValue file_dict;
  file_dict.Set("filename", block.filename);
  file_dict.Set("file_data", block.file_data.spec());
  return file_dict;
}

base::DictValue ImageContentBlockToDict(const mojom::ImageContentBlock& block) {
  base::DictValue image_url;
  image_url.Set("url", block.image_url.spec());
  return image_url;
}

void SerializeToolCallsOnMessageDict(const OAIMessage& message,
                                     base::DictValue& message_dict) {
  if (!message.tool_calls.empty()) {
    base::ListValue tool_call_dicts;
    for (const auto& tool_event : message.tool_calls) {
      base::DictValue tool_call_dict;
      tool_call_dict.Set("id", tool_event->id);
      tool_call_dict.Set("type", "function");

      base::DictValue function_dict;
      function_dict.Set("name", tool_event->tool_name);
      // Some models emit an empty string for the arguments of a tool that takes
      // no parameters. An empty string is not valid JSON, so the server rejects
      // the tool call ("invalid json arguments"). Normalize to an empty object.
      function_dict.Set("arguments", tool_event->arguments_json.empty()
                                         ? "{}"
                                         : tool_event->arguments_json);

      tool_call_dict.Set("function", std::move(function_dict));
      tool_call_dicts.Append(std::move(tool_call_dict));
    }

    message_dict.Set("tool_calls", std::move(tool_call_dicts));
  }

  if (!message.tool_call_id.empty()) {
    message_dict.Set("tool_call_id", message.tool_call_id);
  }
}

base::ListValue ConvertOAIMessagesToResponsesApiInput(
    const base::ListValue& chat_messages) {
  base::ListValue input;
  for (const auto& message_value : chat_messages) {
    if (!message_value.is_dict()) {
      continue;
    }
    const base::DictValue& message = message_value.GetDict();
    const std::string* role = message.FindString("role");
    if (!role) {
      continue;
    }

    // A tool-result message has no "role" concept in the Responses API -
    // it's a standalone function_call_output item keyed by call_id.
    if (*role == "tool") {
      const std::string* tool_call_id = message.FindString("tool_call_id");
      base::DictValue item;
      item.Set("type", "function_call_output");
      item.Set("call_id", tool_call_id ? *tool_call_id : "");
      item.Set("output", FlattenTextContentBlocks(message.FindList("content")));
      input.Append(std::move(item));
      continue;
    }

    // "system" has no role of that name in the Responses API - the
    // equivalent is "developer". Assistant text is echoed back as
    // "output_text" (matching what the API itself would have returned),
    // everything else as "input_text".
    std::string responses_role = (*role == "system") ? "developer" : *role;
    bool is_assistant = (*role == "assistant");

    const base::ListValue* content = message.FindList("content");
    if (content && !content->empty()) {
      base::ListValue content_items;
      for (const auto& block_value : *content) {
        if (!block_value.is_dict()) {
          continue;
        }
        const base::DictValue& block = block_value.GetDict();
        const std::string* block_type = block.FindString("type");
        if (!block_type) {
          continue;
        }
        if (*block_type == "text") {
          const std::string* text = block.FindString("text");
          base::DictValue item;
          item.Set("type", is_assistant ? "output_text" : "input_text");
          item.Set("text", text ? *text : "");
          content_items.Append(std::move(item));
        } else if (*block_type == "image_url") {
          const base::DictValue* image_url_dict = block.FindDict("image_url");
          const std::string* url =
              image_url_dict ? image_url_dict->FindString("url") : nullptr;
          if (url) {
            base::DictValue item;
            item.Set("type", "input_image");
            item.Set("image_url", *url);
            content_items.Append(std::move(item));
          }
        }
        // Other content block types aren't yet supported on this path -
        // dropped rather than guessed at.
      }
      if (!content_items.empty()) {
        base::DictValue message_item;
        message_item.Set("type", "message");
        message_item.Set("role", responses_role);
        message_item.Set("content", std::move(content_items));
        input.Append(std::move(message_item));
      }
    }

    // An assistant message's tool_calls each become their own top-level
    // function_call item, not part of the message item above.
    if (const base::ListValue* tool_calls = message.FindList("tool_calls")) {
      for (const auto& tool_call_value : *tool_calls) {
        if (!tool_call_value.is_dict()) {
          continue;
        }
        const base::DictValue& tool_call = tool_call_value.GetDict();
        const base::DictValue* function = tool_call.FindDict("function");
        const std::string* call_id = tool_call.FindString("id");
        const std::string* name =
            function ? function->FindString("name") : nullptr;
        const std::string* arguments =
            function ? function->FindString("arguments") : nullptr;
        base::DictValue item;
        item.Set("type", "function_call");
        item.Set("call_id", call_id ? *call_id : "");
        item.Set("name", name ? *name : "");
        item.Set("arguments", arguments ? *arguments : "{}");
        input.Append(std::move(item));
      }
    }
  }
  return input;
}

}  // namespace ai_chat

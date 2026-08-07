/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'

import { I18nMixin, I18nMixinInterface } from
  'chrome://resources/cr_elements/i18n_mixin.js'
import { PolymerElement } from
  'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import { BaseMixin, BaseMixinInterface } from '../base_mixin.js'
import {
  AIChatConversationListItem,
  BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl
} from './brave_leo_assistant_browser_proxy.js'
import { getTemplate } from './ai_chat_history_section.html.js'

interface AIChatConversationDisplayItem extends AIChatConversationListItem {
  updatedTimeLabel: string
}

const AIChatHistorySectionBase = I18nMixin(BaseMixin(PolymerElement)) as {
  new (): PolymerElement & I18nMixinInterface & BaseMixinInterface
}

class AIChatHistorySection extends AIChatHistorySectionBase {
  static get is() {
    return 'ai-chat-history-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      conversations_: {
        type: Array,
        value: []
      },
      isLoading_: {
        type: Boolean,
        value: false
      }
    }
  }

  browserProxy_: BraveLeoAssistantBrowserProxy =
    BraveLeoAssistantBrowserProxyImpl.getInstance()
  declare conversations_: AIChatConversationDisplayItem[]
  declare isLoading_: boolean

  override ready() {
    super.ready()
    this.loadData_()
  }

  loadData_() {
    this.isLoading_ = true
    this.browserProxy_.getAIChatConversations().then((conversations) => {
      // Newest first.
      this.conversations_ = conversations
        .slice()
        .sort((a, b) => b.updatedTimeMs - a.updatedTimeMs)
        .map((conversation) => ({
          ...conversation,
          updatedTimeLabel: new Date(conversation.updatedTimeMs)
            .toLocaleString()
        }))
      this.isLoading_ = false
    })
  }

  onRefreshClick_() {
    this.loadData_()
  }

  onConversationClick_(e: { model: { item: AIChatConversationDisplayItem } }) {
    this.browserProxy_.openAIChatConversation(e.model.item.uuid)
  }

  hasConversations_(conversations: AIChatConversationDisplayItem[]): boolean {
    return conversations.length > 0
  }

  getConversationTitle_(conversation: AIChatConversationDisplayItem): string {
    return conversation.title || this.i18n('aiChatHistoryUntitledLabel')
  }
}

customElements.define(AIChatHistorySection.is, AIChatHistorySection)

declare global {
  interface HTMLElementTagNameMap {
    'ai-chat-history-section': AIChatHistorySection
  }
}

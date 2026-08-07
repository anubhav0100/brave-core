/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js'
import 'chrome://resources/cr_elements/cr_input/cr_input.js'
import 'chrome://resources/cr_elements/icons.html.js'

import { I18nMixin, I18nMixinInterface } from
  'chrome://resources/cr_elements/i18n_mixin.js'
import { PolymerElement } from
  'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import { BaseMixin, BaseMixinInterface } from '../base_mixin.js'
import {
  BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl,
  WebhookToolListItem,
  WebhookToolParameter
} from './brave_leo_assistant_browser_proxy.js'
import { getTemplate } from './webhook_tools_section.html.js'

// Parses "name | description | required(yes/no)" lines, one parameter per
// line, into the shape the backend expects - a plain textarea is far
// simpler to get right in this UI than a fully dynamic add/remove-row form,
// while still producing real JSON-Schema properties for the tool.
function parseParametersText(text: string): WebhookToolParameter[] {
  return text
    .split('\n')
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .map((line) => {
      const parts = line.split('|').map((part) => part.trim())
      return {
        name: parts[0] || '',
        description: parts[1] || '',
        required: (parts[2] || '').toLowerCase().startsWith('y')
      }
    })
    .filter((param) => param.name.length > 0)
}

function formatParametersText(parameters: WebhookToolParameter[]): string {
  return parameters
    .map((param) =>
      `${param.name} | ${param.description} | ${param.required ? 'yes' : 'no'}`)
    .join('\n')
}

const WebhookToolsSectionBase = I18nMixin(BaseMixin(PolymerElement)) as {
  new (): PolymerElement & I18nMixinInterface & BaseMixinInterface
}

class WebhookToolsSection extends WebhookToolsSectionBase {
  static get is() {
    return 'webhook-tools-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      tools_: {
        type: Array,
        value: []
      },
      showEditDialog_: {
        type: Boolean,
        value: false
      },
      showDeleteDialog_: {
        type: Boolean,
        value: false
      },
      editingId_: {
        type: String,
        value: null
      },
      deletingId_: {
        type: String,
        value: null
      },
      editName_: { type: String, value: '' },
      editDescription_: { type: String, value: '' },
      editUrl_: { type: String, value: '' },
      editSecret_: { type: String, value: '' },
      editEnabled_: { type: Boolean, value: true },
      editParametersText_: { type: String, value: '' },
      editHasExistingSecret_: { type: Boolean, value: false },
      isEditingExisting_: {
        type: Boolean,
        computed: 'computeIsEditingExisting_(editingId_)'
      }
    }
  }

  browserProxy_: BraveLeoAssistantBrowserProxy =
    BraveLeoAssistantBrowserProxyImpl.getInstance()
  declare tools_: WebhookToolListItem[]
  declare showEditDialog_: boolean
  declare showDeleteDialog_: boolean
  declare editingId_: string | null
  declare deletingId_: string | null
  declare editName_: string
  declare editDescription_: string
  declare editUrl_: string
  declare editSecret_: string
  declare editEnabled_: boolean
  declare editParametersText_: string
  declare editHasExistingSecret_: boolean
  declare isEditingExisting_: boolean

  override ready() {
    super.ready()
    this.loadTools_()
  }

  loadTools_() {
    this.browserProxy_.getWebhookTools().then((tools: WebhookToolListItem[]) => {
      this.tools_ = tools
    })
  }

  computeIsEditingExisting_(editingId: string | null): boolean {
    return editingId !== null
  }

  hasTools_(tools: WebhookToolListItem[]): boolean {
    return tools.length > 0
  }

  handleAddNewClick_() {
    this.editingId_ = null
    this.editName_ = ''
    this.editDescription_ = ''
    this.editUrl_ = ''
    this.editSecret_ = ''
    this.editEnabled_ = true
    this.editParametersText_ = ''
    this.editHasExistingSecret_ = false
    this.showEditDialog_ = true
  }

  handleEditClick_(e: { model: { item: WebhookToolListItem } }) {
    const item = e.model.item
    this.editingId_ = item.id
    this.editName_ = item.name
    this.editDescription_ = item.description
    this.editUrl_ = item.url
    this.editSecret_ = ''
    this.editEnabled_ = item.enabled
    this.editParametersText_ = formatParametersText(item.parameters)
    this.editHasExistingSecret_ = item.hasSecret
    this.showEditDialog_ = true
  }

  handleDeleteClick_(e: { model: { item: WebhookToolListItem } }) {
    this.deletingId_ = e.model.item.id
    this.showDeleteDialog_ = true
  }

  handleToggleEnabled_(e: { model: { item: WebhookToolListItem } }) {
    const item = e.model.item
    this.browserProxy_
      .updateWebhookTool({
        id: item.id,
        name: item.name,
        description: item.description,
        url: item.url,
        secret: '',
        enabled: !item.enabled,
        parameters: item.parameters
      })
      .then(() => this.loadTools_())
  }

  onNameInput_(e: { value: string }) {
    this.editName_ = e.value
  }

  onDescriptionInput_(e: { value: string }) {
    this.editDescription_ = e.value
  }

  onUrlInput_(e: { value: string }) {
    this.editUrl_ = e.value
  }

  onSecretInput_(e: { value: string }) {
    this.editSecret_ = e.value
  }

  onEnabledChange_(e: { detail: { checked: boolean } }) {
    this.editEnabled_ = e.detail.checked
  }

  onParametersTextInput_(e: { value: string }) {
    this.editParametersText_ = e.value
  }

  isSaveDisabled_(name: string, url: string): boolean {
    return !name.trim() || !url.trim()
  }

  onDialogCancel_() {
    this.showEditDialog_ = false
  }

  onDialogSave_() {
    const payload = {
      id: this.editingId_ || undefined,
      name: this.editName_.trim(),
      description: this.editDescription_.trim(),
      url: this.editUrl_.trim(),
      secret: this.editSecret_,
      enabled: this.editEnabled_,
      parameters: parseParametersText(this.editParametersText_)
    }

    const request = this.editingId_
      ? this.browserProxy_.updateWebhookTool(
          { ...payload, id: this.editingId_ })
      : this.browserProxy_.addWebhookTool(payload)

    request.then(() => {
      this.showEditDialog_ = false
      this.loadTools_()
    })
  }

  onDeleteDialogCancel_() {
    this.showDeleteDialog_ = false
    this.deletingId_ = null
  }

  onDeleteDialogConfirm_() {
    if (this.deletingId_) {
      this.browserProxy_.deleteWebhookTool(this.deletingId_).then(() => {
        this.loadTools_()
      })
    }
    this.showDeleteDialog_ = false
    this.deletingId_ = null
  }

  getDialogTitle_(editingId: string | null): string {
    return editingId === null
      ? this.i18n('webhookToolAddDialogTitle')
      : this.i18n('webhookToolEditDialogTitle')
  }
}

customElements.define(WebhookToolsSection.is, WebhookToolsSection)

declare global {
  interface HTMLElementTagNameMap {
    'webhook-tools-section': WebhookToolsSection
  }
}

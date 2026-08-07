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
  BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl
} from './brave_leo_assistant_browser_proxy.js'
import { getTemplate } from './page_capture_section.html.js'

export interface PageCaptureEntry {
  heading: string
  preview: string
}

export interface PageCaptureLogEntry {
  timestampMs: number
  message: string
  time: string
}

const PageCaptureSectionBase = I18nMixin(BaseMixin(PolymerElement)) as {
  new (): PolymerElement & I18nMixinInterface & BaseMixinInterface
}

class PageCaptureSection extends PageCaptureSectionBase {
  static get is() {
    return 'page-capture-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      entries_: {
        type: Array,
        value: []
      },
      log_: {
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
  declare entries_: PageCaptureEntry[]
  declare log_: PageCaptureLogEntry[]
  declare isLoading_: boolean

  override ready() {
    super.ready()
    this.loadData_()
  }

  loadData_() {
    this.isLoading_ = true
    this.browserProxy_.getPageCaptureData().then((result) => {
      this.entries_ = result.entries
      // Newest first, formatted for display.
      this.log_ = result.log
        .map((entry) => ({
          ...entry,
          time: new Date(entry.timestampMs).toLocaleTimeString()
        }))
        .reverse()
      this.isLoading_ = false
    })
  }

  onRefreshClick_() {
    this.loadData_()
  }

  hasEntries_(entries: PageCaptureEntry[]): boolean {
    return entries.length > 0
  }

  hasLog_(log: PageCaptureLogEntry[]): boolean {
    return log.length > 0
  }
}

customElements.define(PageCaptureSection.is, PageCaptureSection)

declare global {
  interface HTMLElementTagNameMap {
    'page-capture-section': PageCaptureSection
  }
}

/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'

import { WebUiListenerMixin, WebUiListenerMixinInterface } from
  'chrome://resources/cr_elements/web_ui_listener_mixin.js'
import { I18nMixin, I18nMixinInterface } from
  'chrome://resources/cr_elements/i18n_mixin.js'
import { PolymerElement } from
  'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import { BaseMixin, BaseMixinInterface } from '../base_mixin.js'
import {
  BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl
} from './brave_leo_assistant_browser_proxy.js'
import { getTemplate } from './n8n_terminal_section.html.js'

// Roughly matches the backend's own cap (N8nProcessManager::
// kMaxOutputBufferBytes) - a client-side ceiling too, in case a very long
// session accumulates more than the backend has already trimmed away.
const MAX_DISPLAYED_OUTPUT_CHARS = 512 * 1024

const N8nTerminalSectionBase = WebUiListenerMixin(
  I18nMixin(BaseMixin(PolymerElement))) as {
  new (): PolymerElement & WebUiListenerMixinInterface &
    I18nMixinInterface & BaseMixinInterface
}

class N8nTerminalSection extends N8nTerminalSectionBase {
  static get is() {
    return 'n8n-terminal-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      running_: { type: Boolean, value: false },
      starting_: { type: Boolean, value: false },
      baseUrl_: { type: String, value: '' },
      outputText_: { type: String, value: '' }
    }
  }

  browserProxy_: BraveLeoAssistantBrowserProxy =
    BraveLeoAssistantBrowserProxyImpl.getInstance()
  declare running_: boolean
  declare starting_: boolean
  declare baseUrl_: string
  declare outputText_: string

  override ready() {
    super.ready()

    this.addWebUiListener(
      'n8n-output-appended', (chunk: string) => this.appendOutput_(chunk))
    this.addWebUiListener(
      'n8n-running-state-changed',
      (running: boolean) => { this.running_ = running })

    this.browserProxy_.getN8nStatus().then((status) => {
      this.running_ = status.running
      this.baseUrl_ = status.baseUrl
    })
    this.browserProxy_.getN8nBufferedOutput().then((output) => {
      this.outputText_ = output
      this.scrollToBottom_()
    })
  }

  appendOutput_(chunk: string) {
    const combined = this.outputText_ + chunk
    this.outputText_ = combined.length > MAX_DISPLAYED_OUTPUT_CHARS
      ? combined.slice(combined.length - MAX_DISPLAYED_OUTPUT_CHARS)
      : combined
    this.scrollToBottom_()
  }

  scrollToBottom_() {
    // Runs after the dom-bind update triggered by outputText_ changing has
    // actually painted, so scrollHeight reflects the new content.
    requestAnimationFrame(() => {
      const terminal = this.shadowRoot?.querySelector('.terminal-output')
      if (terminal) {
        terminal.scrollTop = terminal.scrollHeight
      }
    })
  }

  handleStartClick_() {
    this.starting_ = true
    this.browserProxy_.startN8n().then((success) => {
      this.starting_ = false
      this.running_ = success
      if (success) {
        this.browserProxy_.getN8nStatus().then((status) => {
          this.baseUrl_ = status.baseUrl
        })
      }
    })
  }

  handleClearViewClick_() {
    // Clears only this view's display - the backend keeps its own buffer,
    // so reopening the page (or a future capture) still shows real history.
    this.outputText_ = ''
  }

  getStatusLabel_(running: boolean): string {
    return running
      ? this.i18n('n8nTerminalStatusRunning')
      : this.i18n('n8nTerminalStatusStopped')
  }

  getStatusDotClass_(running: boolean): string {
    return running ? 'running' : ''
  }

  computeStartDisabled_(running: boolean, starting: boolean): boolean {
    return running || starting
  }

  getStartLabel_(starting: boolean): string {
    return starting
      ? this.i18n('n8nTerminalStartingLabel')
      : this.i18n('n8nTerminalStartLabel')
  }

  hasOutput_(outputText: string): boolean {
    return outputText.length > 0
  }
}

customElements.define(N8nTerminalSection.is, N8nTerminalSection)

declare global {
  interface HTMLElementTagNameMap {
    'n8n-terminal-section': N8nTerminalSection
  }
}

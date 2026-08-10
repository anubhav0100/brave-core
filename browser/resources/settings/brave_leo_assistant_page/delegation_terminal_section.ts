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
import { getTemplate } from './delegation_terminal_section.html.js'

// Same rationale as n8n_terminal_section.ts's cap - a client-side ceiling
// matching DelegationProcessManager::kMaxOutputBufferBytes.
const MAX_DISPLAYED_OUTPUT_CHARS = 512 * 1024

const DelegationTerminalSectionBase = WebUiListenerMixin(
  I18nMixin(BaseMixin(PolymerElement))) as {
  new (): PolymerElement & WebUiListenerMixinInterface &
    I18nMixinInterface & BaseMixinInterface
}

class DelegationTerminalSection extends DelegationTerminalSectionBase {
  static get is() {
    return 'delegation-terminal-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      running_: { type: Boolean, value: false },
      starting_: { type: Boolean, value: false },
      baseUrl_: { type: String, value: '' },
      outputText_: { type: String, value: '' },
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
      'delegation-output-appended',
      (chunk: string) => this.appendOutput_(chunk))
    this.addWebUiListener(
      'delegation-running-state-changed',
      (running: boolean) => { this.running_ = running })

    this.browserProxy_.getDelegationStatus().then((status) => {
      this.running_ = status.running
      this.baseUrl_ = status.baseUrl
    })
    this.browserProxy_.getDelegationBufferedOutput().then((output) => {
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
    requestAnimationFrame(() => {
      const terminal = this.shadowRoot?.querySelector('.terminal-output')
      if (terminal) {
        terminal.scrollTop = terminal.scrollHeight
      }
    })
  }

  handleStartClick_() {
    this.starting_ = true
    this.browserProxy_.startDelegation().then((success) => {
      this.starting_ = false
      this.running_ = success
      if (success) {
        this.browserProxy_.getDelegationStatus().then((status) => {
          this.baseUrl_ = status.baseUrl
        })
      }
    })
  }

  handleClearViewClick_() {
    this.outputText_ = ''
  }

  getStatusLabel_(running: boolean): string {
    return running
      ? this.i18n('delegationTerminalStatusRunning')
      : this.i18n('delegationTerminalStatusStopped')
  }

  getStatusDotClass_(running: boolean): string {
    return running ? 'running' : ''
  }

  computeStartDisabled_(running: boolean, starting: boolean): boolean {
    return running || starting
  }

  getStartLabel_(starting: boolean): string {
    return starting
      ? this.i18n('delegationTerminalStartingLabel')
      : this.i18n('delegationTerminalStartLabel')
  }

  hasOutput_(outputText: string): boolean {
    return outputText.length > 0
  }
}

customElements.define(
  DelegationTerminalSection.is, DelegationTerminalSection)

declare global {
  interface HTMLElementTagNameMap {
    'delegation-terminal-section': DelegationTerminalSection
  }
}

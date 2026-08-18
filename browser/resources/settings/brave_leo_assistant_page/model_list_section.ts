/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/cr_elements/cr_icon/cr_icon.js'
import 'chrome://resources/cr_elements/cr_input/cr_input.js'
import 'chrome://resources/cr_elements/icons.html.js'
import 'chrome://resources/brave/leo.bundle.js'

import { addWebUiListener } from 'chrome://resources/js/cr.js'
import { PrefsMixin } from '/shared/settings/prefs/prefs_mixin.js'
import { I18nMixin } from 'chrome://resources/cr_elements/i18n_mixin.js'
import { PolymerElement } from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import { BaseMixin } from '../base_mixin.js'
import { routes } from '../route.js'
import { Router } from '../router.js'
import { SettingsViewMixin } from '../settings_page/settings_view_mixin.js'

import { getTemplate } from './model_list_section.html.js'
import {
  BraveLeoAssistantBrowserProxyImpl,
  OLLAMA_ENDPOINT
} from './brave_leo_assistant_browser_proxy.js'
import type {
  BraveLeoAssistantBrowserProxy,
  ColibriDownloadState,
  Model
} from './brave_leo_assistant_browser_proxy.js'

// How often to re-poll download progress while a Colibri model download is
// in progress - matches the terminal sections' health-check cadence order
// of magnitude, fast enough to feel live without hammering the endpoint.
const COLIBRI_DOWNLOAD_POLL_INTERVAL_MS = 2000

const ModelListSectionBase =
  PrefsMixin(I18nMixin(BaseMixin(SettingsViewMixin(PolymerElement))))

class ModelListSection extends ModelListSectionBase {
  static get is() {
    return 'model-list-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      customModelsList_: {
        type: Array
      },
      isOllamaConnected_: {
        type: Boolean,
        value: false
      },
      colibriExecutablePath_: {
        type: String,
        value: ''
      },
      colibriModelPath_: {
        type: String,
        value: ''
      },
      colibriRunning_: {
        type: Boolean,
        value: false
      },
      colibriStarting_: {
        type: Boolean,
        value: false
      },
      isColibriConnected_: {
        type: Boolean,
        value: false
      },
      colibriDownloadRepo_: {
        type: String,
        value: ''
      },
      colibriDownloadOutdir_: {
        type: String,
        value: ''
      },
      colibriDownloadStatus_: {
        type: String,
        value: ''
      },
      colibriDownloadProgress_: {
        type: Number,
        value: 0
      },
      colibriDownloadStatusText_: {
        type: String,
        value: ''
      },
      colibriDownloadLastLog_: {
        type: String,
        value: ''
      },
      colibriDownloadSucceeded_: {
        type: Boolean,
        value: false
      }
    }
  }

  browserProxy_: BraveLeoAssistantBrowserProxy =
    BraveLeoAssistantBrowserProxyImpl.getInstance()
  declare customModelsList_: Model[]
  declare isOllamaConnected_: boolean
  declare colibriExecutablePath_: string
  declare colibriModelPath_: string
  declare colibriRunning_: boolean
  declare colibriStarting_: boolean
  declare isColibriConnected_: boolean
  declare colibriDownloadRepo_: string
  declare colibriDownloadOutdir_: string
  declare colibriDownloadStatus_: string
  declare colibriDownloadProgress_: number
  declare colibriDownloadStatusText_: string
  declare colibriDownloadLastLog_: string
  declare colibriDownloadSucceeded_: boolean
  private colibriDownloadPollTimer_: number|null = null

  override ready() {
    super.ready()

    const settingsHelper = this.browserProxy_.getSettingsHelper()

    settingsHelper.getCustomModels().then((value: { models: Model[] }) => {
      this.customModelsList_ = value.models
    })

    this.browserProxy_
      .getCallbackRouter()
      .onModelListChanged.addListener((models: Model[]) => {
        this.customModelsList_ = models
      })

    // Check Ollama connection on page load
    this.checkOllamaConnection_()

    addWebUiListener(
      'colibri-running-state-changed', (running: boolean) => {
        this.colibriRunning_ = running
        if (running) {
          this.checkColibriConnection_()
          this.pollColibriDownloadState_()
        }
      })

    this.browserProxy_.getColibriStatus().then((status) => {
      this.colibriRunning_ = status.running
      this.colibriExecutablePath_ = status.executablePath
      this.colibriModelPath_ = status.modelPath
      if (status.running) {
        // Restore an in-progress (or just-finished) download's state after
        // a Settings page reload, rather than starting blank.
        this.pollColibriDownloadState_()
      }
    })
    this.checkColibriConnection_()
  }

  override getAssociatedControlFor(childViewId: string): HTMLElement {
    switch (childViewId) {
      case 'add-model':
        return this.shadowRoot!.querySelector('#addNewModel')!;
      default:
        throw new Error(`Unknown child view id: ${childViewId}`)
    }
  }

  handleDelete_(e: Event & {model: {index: number}}) {
    const messageText = this.i18n('braveLeoAssistantDeleteModelConfirmation')
    const shouldDeleteModel = confirm(messageText)

    if (!shouldDeleteModel) {
      return
    }

    this.browserProxy_.getSettingsHelper().deleteCustomModel(e.model.index)
  }

  handleEdit_(e: Event & {model: {index: number}}) {
    Router.getInstance().navigateTo(
      routes.BRAVE_LEO_ADD_MODEL,
      new URLSearchParams({index: String(e.model.index)})
    )
  }

  handleAddNewModel_() {
    Router.getInstance().navigateTo(routes.BRAVE_LEO_ADD_MODEL)
  }

  private hasCustomModels_(customModelsList: Model[]) {
    return customModelsList.length > 0
  }

  private async checkOllamaConnection_() {
    try {
      const result = await this.browserProxy_.checkOllamaConnection()
      this.isOllamaConnected_ = result.connected
    } catch (error) {
      console.error('Failed to check Ollama connection:', error)
      this.isOllamaConnected_ = false
    }
  }

  private isOllamaManagedModel_(
      model: Model, ollamaSyncEnabled: boolean,
      isOllamaConnected: boolean): boolean {
    // Only consider it managed if all three:
    // 1. It points to Ollama endpoint
    // 2. Ollama sync preference is enabled
    // 3. Ollama is actually connected
    const isOllamaEndpoint =
        model.options.customModelOptions?.endpoint === OLLAMA_ENDPOINT
    return isOllamaEndpoint && ollamaSyncEnabled && isOllamaConnected
  }

  private async checkColibriConnection_() {
    try {
      const result = await this.browserProxy_.checkColibriConnection()
      this.isColibriConnected_ = result.connected
    } catch (error) {
      console.error('Failed to check Colibri connection:', error)
      this.isColibriConnected_ = false
    }
  }

  onColibriExecutablePathInput_(e: { value: string }) {
    this.colibriExecutablePath_ = e.value
  }

  onColibriModelPathInput_(e: { value: string }) {
    this.colibriModelPath_ = e.value
  }

  handleColibriStartClick_() {
    this.colibriStarting_ = true
    this.browserProxy_
      .startColibri(this.colibriExecutablePath_, this.colibriModelPath_)
      .then((success) => {
        this.colibriStarting_ = false
        this.colibriRunning_ = success
        if (success) {
          this.checkColibriConnection_()
        }
      })
  }

  private getColibriStatusLabel_(running: boolean): string {
    return running
      ? this.i18n('braveLeoAssistantColibriStatusRunning')
      : this.i18n('braveLeoAssistantColibriStatusStopped')
  }

  private getColibriStatusDotClass_(running: boolean): string {
    return running ? 'running' : ''
  }

  private computeColibriStartDisabled_(
      running: boolean, starting: boolean): boolean {
    return running || starting
  }

  private getColibriStartLabel_(starting: boolean): string {
    return starting
      ? this.i18n('braveLeoAssistantColibriStartingLabel')
      : this.i18n('braveLeoAssistantColibriStartLabel')
  }

  private isColibriDownloadActive_(status: string): boolean {
    return status === 'running'
  }

  onColibriDownloadRepoInput_(e: { value: string }) {
    this.colibriDownloadRepo_ = e.value
  }

  onColibriDownloadOutdirInput_(e: { value: string }) {
    this.colibriDownloadOutdir_ = e.value
  }

  handleColibriDownloadClick_() {
    if (!this.colibriDownloadRepo_ || !this.colibriDownloadOutdir_) {
      return
    }
    this.browserProxy_
      .startColibriDownload(
        this.colibriDownloadRepo_, this.colibriDownloadOutdir_)
      .then((response) => {
        this.applyColibriDownloadState_(response.state)
        this.pollColibriDownloadState_()
      })
  }

  handleColibriDownloadStopClick_() {
    this.stopColibriDownloadPolling_()
    this.browserProxy_.stopColibriDownload().then((response) => {
      this.applyColibriDownloadState_(response.state)
    })
  }

  handleLoadDownloadedModelClick_() {
    if (!this.colibriDownloadOutdir_) {
      return
    }
    this.colibriStarting_ = true
    this.browserProxy_.restartColibriWithModel(this.colibriDownloadOutdir_)
      .then((success) => {
        this.colibriStarting_ = false
        this.colibriRunning_ = success
        this.colibriModelPath_ = this.colibriDownloadOutdir_
        if (success) {
          this.colibriDownloadSucceeded_ = false
          this.checkColibriConnection_()
        }
      })
  }

  private pollColibriDownloadState_() {
    this.stopColibriDownloadPolling_()
    this.browserProxy_.getColibriDownloadState().then((state) => {
      this.applyColibriDownloadState_(state)
      if (state.status === 'running') {
        this.colibriDownloadPollTimer_ = setTimeout(
          () => this.pollColibriDownloadState_(),
          COLIBRI_DOWNLOAD_POLL_INTERVAL_MS) as unknown as number
      }
    })
  }

  private stopColibriDownloadPolling_() {
    if (this.colibriDownloadPollTimer_ !== null) {
      clearTimeout(this.colibriDownloadPollTimer_)
      this.colibriDownloadPollTimer_ = null
    }
  }

  private applyColibriDownloadState_(state: ColibriDownloadState) {
    if (!state || !state.status) {
      return
    }
    this.colibriDownloadStatus_ = state.status
    this.colibriDownloadProgress_ = state.progress ?? 0
    this.colibriDownloadSucceeded_ = state.status === 'success'
    const logs = state.logs ?? []
    this.colibriDownloadLastLog_ = logs.length ? logs[logs.length - 1] : ''
    if (state.status === 'running') {
      this.colibriDownloadStatusText_ = this.i18n(
        'braveLeoAssistantColibriDownloadInProgressLabel',
        String(state.progress ?? 0))
    } else if (state.status === 'success') {
      this.colibriDownloadStatusText_ =
        this.i18n('braveLeoAssistantColibriDownloadDoneLabel')
    } else if (state.status === 'error') {
      this.colibriDownloadStatusText_ =
        this.i18n('braveLeoAssistantColibriDownloadErrorLabel')
    } else {
      this.colibriDownloadStatusText_ = ''
    }
    if (!this.colibriDownloadOutdir_ && state.outdir) {
      this.colibriDownloadOutdir_ = state.outdir
    }
    if (!this.colibriDownloadRepo_ && state.repo) {
      this.colibriDownloadRepo_ = state.repo
    }
  }
}

customElements.define(ModelListSection.is, ModelListSection)

// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import '//resources/cr_elements/md_select.css.js'
import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/brave/leo.bundle.js'
import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js'
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'
import {WebUiListenerMixin} from 'chrome://resources/cr_elements/web_ui_listener_mixin.js'
import {PrefsMixin} from '/shared/settings/prefs/prefs_mixin.js'
import {Router} from '../router.js'
import {loadTimeData} from '../i18n_setup.js'
import {routes} from '../route.js';
import {getTemplate} from './brave_leo_assistant_page.html.js'
import {BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl, PremiumStatus,
  PremiumInfo, ModelWithSubtitle}
  from './brave_leo_assistant_browser_proxy.js'
import './model_selector.js'

const BraveLeoAssistantPageBase =
  WebUiListenerMixin(I18nMixin(PrefsMixin(PolymerElement)))

/**
 * 'settings-brave-leo-assistant-page' is the settings page containing
 * brave's Leo Assistant features.
 */
class BraveLeoAssistantPageElement extends BraveLeoAssistantPageBase {
    static get is() {
        return 'settings-brave-leo-assistant-page'
    }

    static get template() {
        return getTemplate()
    }

    static get properties() {
      return {
        leoAssistantShowOnToolbarPref_: {
          type: Boolean,
          value: false,
          notify: true,
        },
        isPremiumUser_: {
          type: Boolean,
          value: false,
          computed: 'computeIsPremiumUser_(premiumStatus_)'
        },
        isHistoryFeatureEnabled_: {
          type: Boolean,
          value: () => loadTimeData.getBoolean('isLeoAssistantHistoryAllowed')
        },
        isTabOrganizationFeatureEnabled_: {
          type: Boolean,
          value: () => loadTimeData.getBoolean(
            'isTabOrganizationFeatureEnabled')
        },
        contentIndexEntryCount_: { type: Number, value: 0 },
        contentIndexAvailable_: { type: Boolean, value: false },
        modelFallbackModels_: { type: Array },
      }
    }

    private declare isPremiumUser_: boolean

    declare isHistoryFeatureEnabled_: boolean
    declare isTabOrganizationFeatureEnabled_: boolean
    declare leoAssistantShowOnToolbarPref_: boolean
    declare contentIndexEntryCount_: number
    declare contentIndexAvailable_: boolean
    declare modelFallbackModels_: ModelWithSubtitle[]
    premiumStatus_: PremiumStatus = PremiumStatus.Unknown
    browserProxy_: BraveLeoAssistantBrowserProxy =
      BraveLeoAssistantBrowserProxyImpl.getInstance()
    manageUrl_: string | undefined = undefined

    onResetAssistantData_() {
      const message =
        this.i18n('braveLeoAssistantResetAndClearDataConfirmationText')
      if(window.confirm(message)) {
        this.browserProxy_.resetLeoData()
      }
    }

    override ready () {
      super.ready()

      this.updateShowLeoAssistantIcon_()
      this.updateCurrentPremiumStatus()
      this.updateContentIndexStatus_()
      this.fetchModelFallbackModels_()

      this.addWebUiListener('settings-brave-leo-assistant-changed',
      (isLeoVisible: boolean) => {
        this.leoAssistantShowOnToolbarPref_ = isLeoVisible
      })

      this.browserProxy_.getSettingsHelper().getManageUrl()
        .then((value: { url: string}) => {
          this.manageUrl_ = value.url
        })

      // Since there is no server-side event for premium status changing,
      // we should check often. And since purchase or login is performed in
      // a separate WebContents, we can check when focus is returned here.
      window.addEventListener('focus', () => {
        this.updateCurrentPremiumStatus()
      })
    }

    itemPref_(enabled: boolean) {
      return {
        key: '',
        type: chrome.settingsPrivate.PrefType.BOOLEAN,
        value: enabled,
      }
    }


    private updateShowLeoAssistantIcon_() {
      this.browserProxy_.getLeoIconVisibility().then((result) => {
        this.leoAssistantShowOnToolbarPref_ = result
      })
    }

    private updateCurrentPremiumStatus() {
      this.browserProxy_.getSettingsHelper().getPremiumStatus().then((value: { status: PremiumStatus; info: PremiumInfo | null; }) => {
        this.premiumStatus_ = value.status
      })
    }

    onLeoAssistantShowOnToolbarChange_(e: Event) {
      e.stopPropagation()
      this.browserProxy_.toggleLeoIcon()
    }

    openAutocompleteSetting_() {
      Router.getInstance().navigateTo(routes.APPEARANCE, new URLSearchParams("highlight=#autocomplete-suggestion-sources"))
    }

    computeIsPremiumUser_() {
      if (this.premiumStatus_ === PremiumStatus.Active || this.premiumStatus_ === PremiumStatus.ActiveDisconnected) {
        return true
      }

      return false
    }

    openManageAccountPage_() {
      window.open(this.manageUrl_, "_self", "noopener noreferrer")
    }

    openTabOrganizationLearnMore_() {
      window.open(loadTimeData.getString('braveLeoAssistantTabOrganizationLearnMoreURL'), "_blank", "noopener noreferrer")
    }

    openCustomizationPage_() {
      const router = Router.getInstance();
      router.navigateTo(router.getRoutes().BRAVE_LEO_CUSTOMIZATION);
    }

    updateContentIndexStatus_() {
      this.browserProxy_.getContentIndexStatus().then((status) => {
        this.contentIndexEntryCount_ = status.entryCount
        this.contentIndexAvailable_ = status.available
      })
    }

    onContentIndexingToggleChange_() {
      // Give the pref write a moment to land before re-checking status.
      setTimeout(() => this.updateContentIndexStatus_(), 0)
    }

    hasContentIndexEntries_(count: number): boolean {
      return count > 0
    }

    contentIndexStatusText_(count: number, available: boolean): string {
      if (!available) {
        return this.i18n('braveLeoAssistantContentIndexingUnavailable')
      }
      return this.i18n('braveLeoAssistantContentIndexingItemCount',
                       String(count))
    }

    onClearContentIndex_() {
      this.browserProxy_.clearContentIndex().then(() => {
        this.updateContentIndexStatus_()
      })
    }

    private fetchModelFallbackModels_() {
      this.browserProxy_.getSettingsHelper().getModelsWithSubtitles()
        .then((value: { models: ModelWithSubtitle[]; }) => {
          this.modelFallbackModels_ = value.models
        })
    }

    onModelFallbackModelChange_(e: CustomEvent<{value: string}>) {
      this.setPrefValue('brave.ai_chat.model_fallback_model_key',
        e.detail.value)
    }
}

customElements.define(
  BraveLeoAssistantPageElement.is, BraveLeoAssistantPageElement)

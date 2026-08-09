/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

 import {sendWithPromise} from 'chrome://resources/js/cr.js';
 import * as mojom from '../settings_helper.mojom-webui.js'
 import * as mojomCustomizationSettings from
   '../customization_settings.mojom-webui.js'
 import {OllamaService, OLLAMA_ENDPOINT} from '../ollama.mojom-webui.js'
 export * from '../ai_chat.mojom-webui.js'
 export * from '../common.mojom-webui.js'
 export * from '../settings_helper.mojom-webui.js'
 export * from '../customization_settings.mojom-webui.js'
 export {OLLAMA_ENDPOINT} from '../ollama.mojom-webui.js'

 export interface WebhookToolParameter {
  name: string
  description: string
  required: boolean
 }

 export interface WebhookToolDto {
  id?: string
  name: string
  description: string
  url: string
  secret: string
  enabled: boolean
  parameters: WebhookToolParameter[]
 }

 export interface WebhookToolListItem {
  id: string
  name: string
  description: string
  url: string
  enabled: boolean
  hasSecret: boolean
  parameters: WebhookToolParameter[]
 }

 export interface AIChatConversationListItem {
  uuid: string
  title: string
  updatedTimeMs: number
 }

 export interface WorkflowListItem {
  id: string
  name: string
  version: string
  status: string
  definitionJson: string
 }

 export interface WorkflowValidationError {
  stepId: string
  message: string
 }

 export interface SaveWorkflowResult {
  id?: string
  errors: WorkflowValidationError[]
 }

 export interface RunWorkflowResult {
  success: boolean
  errorMessage: string
  executedStepIds: string[]
  outputs: Record<string, string>
 }

 export interface BraveLeoAssistantBrowserProxy {
  resetLeoData(): void
  getLeoIconVisibility(): Promise<boolean>
  toggleLeoIcon(): void
  getSettingsHelper(): mojom.AIChatSettingsHelperRemote
  getCallbackRouter(): mojom.SettingsPageCallbackRouter
  getCustomizationSettingsHandler():
    mojomCustomizationSettings.CustomizationSettingsHandlerRemote
  getCustomizationSettingsCallbackRouter():
    mojomCustomizationSettings.CustomizationSettingsUICallbackRouter
  checkOllamaConnection(): Promise<{connected: boolean}>
  fetchAvailableModels(endpoint: string, apiKey: string): Promise<string[]>
  getPageCaptureData(): Promise<{
    entries: {heading: string, preview: string}[],
    log: {timestampMs: number, message: string}[]
  }>
  getWebhookTools(): Promise<WebhookToolListItem[]>
  addWebhookTool(tool: WebhookToolDto): Promise<string>
  updateWebhookTool(tool: WebhookToolDto): Promise<boolean>
  deleteWebhookTool(id: string): Promise<boolean>
  getAIChatConversations(): Promise<AIChatConversationListItem[]>
  openAIChatConversation(uuid: string): Promise<boolean>
  getWorkflows(): Promise<WorkflowListItem[]>
  saveWorkflow(definitionJson: string): Promise<SaveWorkflowResult>
  publishWorkflow(id: string): Promise<boolean>
  deleteWorkflow(id: string): Promise<boolean>
  runWorkflow(id: string): Promise<RunWorkflowResult>
  getContentIndexStatus(): Promise<{
    entryCount: number, available: boolean, enabled: boolean
  }>
  clearContentIndex(): Promise<boolean>
  getN8nStatus(): Promise<{running: boolean, baseUrl: string}>
  getN8nBufferedOutput(): Promise<string>
  startN8n(): Promise<boolean>
 }

 export class BraveLeoAssistantBrowserProxyImpl
    implements BraveLeoAssistantBrowserProxy {
   settingsHelper: mojom.AIChatSettingsHelperRemote
   callbackRouter: mojom.SettingsPageCallbackRouter
   customizationSettingsHandler:
     mojomCustomizationSettings.CustomizationSettingsHandlerRemote
   customizationSettingsCallbackRouter:
     mojomCustomizationSettings.CustomizationSettingsUICallbackRouter
   ollamaService: ReturnType<typeof OllamaService.getRemote>

   private constructor() {
      this.settingsHelper = mojom.AIChatSettingsHelper.getRemote()
      this.callbackRouter = new mojom.SettingsPageCallbackRouter()
      this.settingsHelper.setClientPage(
        this.callbackRouter.$.bindNewPipeAndPassRemote())

      this.customizationSettingsHandler =
        mojomCustomizationSettings.CustomizationSettingsHandler.getRemote()
      this.customizationSettingsCallbackRouter =
        new mojomCustomizationSettings.CustomizationSettingsUICallbackRouter()
      this.customizationSettingsHandler.bindUI(
        this.customizationSettingsCallbackRouter.$.bindNewPipeAndPassRemote())

      this.ollamaService = OllamaService.getRemote()
   }

   static getInstance(): BraveLeoAssistantBrowserProxyImpl {
     return instance || (instance = new BraveLeoAssistantBrowserProxyImpl())
   }

   getLeoIconVisibility() {
     return sendWithPromise<boolean>('getLeoIconVisibility')
   }

   toggleLeoIcon() {
     chrome.send('toggleLeoIcon')
   }

   resetLeoData() {
     chrome.send('resetLeoData')
   }

   getSettingsHelper() {
     return this.settingsHelper
   }

   getCallbackRouter() {
     return this.callbackRouter
   }

   getCustomizationSettingsHandler() {
     return this.customizationSettingsHandler
   }

   getCustomizationSettingsCallbackRouter() {
     return this.customizationSettingsCallbackRouter
   }

   async checkOllamaConnection() {
     const result = await this.ollamaService.isConnected()
     return {
       connected: result.connected
     }
   }

   fetchAvailableModels(endpoint: string, apiKey: string) {
     return sendWithPromise<string[]>(
       'fetchAvailableModels', { endpoint, apiKey })
   }

   getPageCaptureData() {
     return sendWithPromise<{
       entries: {heading: string, preview: string}[],
       log: {timestampMs: number, message: string}[]
     }>('getPageCaptureData')
   }

   getWebhookTools() {
     return sendWithPromise<WebhookToolListItem[]>('getWebhookTools')
   }

   addWebhookTool(tool: WebhookToolDto) {
     return sendWithPromise<string>('addWebhookTool', tool)
   }

   updateWebhookTool(tool: WebhookToolDto) {
     return sendWithPromise<boolean>('updateWebhookTool', tool)
   }

   deleteWebhookTool(id: string) {
     return sendWithPromise<boolean>('deleteWebhookTool', id)
   }

   getAIChatConversations() {
     return sendWithPromise<AIChatConversationListItem[]>(
       'getAIChatConversations')
   }

   openAIChatConversation(uuid: string) {
     return sendWithPromise<boolean>('openAIChatConversation', uuid)
   }

   getWorkflows() {
     return sendWithPromise<WorkflowListItem[]>('getWorkflows')
   }

   saveWorkflow(definitionJson: string) {
     return sendWithPromise<SaveWorkflowResult>('saveWorkflow', definitionJson)
   }

   publishWorkflow(id: string) {
     return sendWithPromise<boolean>('publishWorkflow', id)
   }

   deleteWorkflow(id: string) {
     return sendWithPromise<boolean>('deleteWorkflow', id)
   }

   runWorkflow(id: string) {
     return sendWithPromise<RunWorkflowResult>('runWorkflow', id)
   }

   getContentIndexStatus() {
     return sendWithPromise<{
       entryCount: number, available: boolean, enabled: boolean
     }>('getContentIndexStatus')
   }

   clearContentIndex() {
     return sendWithPromise<boolean>('clearContentIndex')
   }

   getN8nStatus() {
     return sendWithPromise<{running: boolean, baseUrl: string}>(
       'getN8nStatus')
   }

   getN8nBufferedOutput() {
     return sendWithPromise<string>('getN8nBufferedOutput')
   }

   startN8n() {
     return sendWithPromise<boolean>('startN8n')
   }
 }

 let instance: BraveLeoAssistantBrowserProxyImpl|null = null

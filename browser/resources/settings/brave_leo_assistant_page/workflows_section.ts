/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js'
import 'chrome://resources/cr_elements/cr_input/cr_input.js'

import { I18nMixin, I18nMixinInterface } from
  'chrome://resources/cr_elements/i18n_mixin.js'
import { PolymerElement } from
  'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import { BaseMixin, BaseMixinInterface } from '../base_mixin.js'
import {
  BraveLeoAssistantBrowserProxy,
  BraveLeoAssistantBrowserProxyImpl,
  RunWorkflowResult,
  WorkflowListItem,
  WorkflowValidationError
} from './brave_leo_assistant_browser_proxy.js'
import { getTemplate } from './workflows_section.html.js'

// The step "type" values the runtime understands - kept in sync with
// WorkflowStepTypeFromString in workflow_definition.cc. call_flow/for_each/
// while/until/break/continue are Phase 4-5 (nested workflows, loops);
// ai.extract/ai.decide are Phase 6's two bounded AI steps (ai.action, the
// open-ended one, isn't implemented - see workflow_runtime.h).
const kStepTypes = [
  'start', 'set_variable', 'condition', 'browser.navigate', 'browser.click',
  'browser.type', 'browser.wait', 'complete', 'fail', 'call_flow',
  'for_each', 'while', 'until', 'break', 'continue', 'ai.extract',
  'ai.decide'
]

// One step, in a shape convenient for two-way-bound form fields - every
// possible field is present as a string regardless of `type`; only the
// fields relevant to `type` are read back out when converting to JSON. This
// mirrors WorkflowStep in workflow_definition.h, which similarly only reads
// the fields relevant to each step's type.
interface WorkflowStepForm {
  id: string
  type: string
  next: string
  variableName: string
  variableValue: string
  conditionExpression: string
  onTrue: string
  onFalse: string
  url: string
  selector: string
  text: string
  seconds: string
  failReason: string
  outputsText: string  // "name=expression" lines, one per output.
  // call_flow
  flowId: string
  callInputsText: string  // "child_input_name=parent expression" lines.
  callOutputsText: string  // "child_output_name=parent_variable_name" lines.
  onChildFailure: string  // "fail_parent" or "continue".
  // for_each (items/itemVariable/indexVariable) and while/until
  // (loopCondition) - bodyStart/maxIterations are shared by all three.
  itemsExpression: string
  itemVariable: string
  indexVariable: string
  loopCondition: string
  bodyStart: string
  maxIterations: string
  // ai.extract (instruction/schema/outputVariable) and ai.decide
  // (instruction/outputVariable shared, plus allowedOutcomesText).
  aiInstruction: string
  aiSchemaJson: string
  aiOutputVariable: string
  allowedOutcomesText: string  // comma- or newline-separated.
}

function newStep(type: string): WorkflowStepForm {
  return {
    id: '', type, next: '', variableName: '', variableValue: '',
    conditionExpression: '', onTrue: '', onFalse: '', url: '', selector: '',
    text: '', seconds: '2', failReason: '', outputsText: '',
    flowId: '', callInputsText: '', callOutputsText: '',
    onChildFailure: 'fail_parent', itemsExpression: '', itemVariable: '',
    indexVariable: '', loopCondition: '', bodyStart: '',
    maxIterations: '1000', aiInstruction: '', aiSchemaJson: '',
    aiOutputVariable: '', allowedOutcomesText: ''
  }
}

function stepNeedsNext(type: string): boolean {
  return type === 'start' || type === 'set_variable' ||
    type === 'browser.navigate' || type === 'browser.click' ||
    type === 'browser.type' || type === 'browser.wait' ||
    type === 'call_flow' || type === 'for_each' || type === 'while' ||
    type === 'until' || type === 'ai.extract' || type === 'ai.decide'
}

// Parses "key=value" lines (one per line) into an object - shared by
// complete's outputs and call_flow's inputs/outputs.
function parseKeyValueLines(text: string): Record<string, string> {
  const result: Record<string, string> = {}
  for (const line of text.split('\n')) {
    const eq = line.indexOf('=')
    if (eq <= 0) {
      continue
    }
    result[line.slice(0, eq).trim()] = line.slice(eq + 1).trim()
  }
  return result
}

function formatKeyValueLines(obj: Record<string, unknown>): string {
  return Object.entries(obj).map(([k, v]) => `${k}=${v}`).join('\n')
}

// Builds the JSON schema workflow_definition.cc's parser reads (see its
// "Complete Workflow Example" companion doc) from the form's current state.
function stepFormToJson(step: WorkflowStepForm): Record<string, unknown> {
  const json: Record<string, unknown> = { id: step.id, type: step.type }
  if (stepNeedsNext(step.type)) {
    json['next'] = step.next
  }
  switch (step.type) {
    case 'set_variable':
      json['name'] = step.variableName
      json['value'] = step.variableValue
      break
    case 'condition':
      json['expression'] = step.conditionExpression
      json['on_true'] = step.onTrue
      json['on_false'] = step.onFalse
      break
    case 'browser.navigate':
      json['url'] = step.url
      break
    case 'browser.click':
      json['selector'] = step.selector
      break
    case 'browser.type':
      json['selector'] = step.selector
      json['text'] = step.text
      break
    case 'browser.wait':
      json['seconds'] = parseInt(step.seconds, 10) || 0
      break
    case 'fail':
      json['reason'] = step.failReason
      break
    case 'complete':
      json['outputs'] = parseKeyValueLines(step.outputsText)
      break
    case 'call_flow':
      json['flow_id'] = step.flowId
      json['on_child_failure'] = step.onChildFailure || 'fail_parent'
      json['inputs'] = parseKeyValueLines(step.callInputsText)
      json['outputs'] = parseKeyValueLines(step.callOutputsText)
      break
    case 'for_each':
      json['items'] = step.itemsExpression
      json['item_variable'] = step.itemVariable
      if (step.indexVariable) {
        json['index_variable'] = step.indexVariable
      }
      json['body_start'] = step.bodyStart
      json['max_iterations'] = parseInt(step.maxIterations, 10) || 1000
      break
    case 'while':
    case 'until':
      json['condition'] = step.loopCondition
      json['body_start'] = step.bodyStart
      json['max_iterations'] = parseInt(step.maxIterations, 10) || 1000
      break
    case 'ai.extract':
      json['instruction'] = step.aiInstruction
      json['schema'] = step.aiSchemaJson
      json['output_variable'] = step.aiOutputVariable
      break
    case 'ai.decide':
      json['instruction'] = step.aiInstruction
      json['output_variable'] = step.aiOutputVariable
      json['allowed_outcomes'] = step.allowedOutcomesText
        .split(/[\n,]/)
        .map((s) => s.trim())
        .filter((s) => s.length > 0)
      break
  }
  return json
}

function stepFromJson(json: Record<string, unknown>): WorkflowStepForm {
  const str = (key: string) =>
    typeof json[key] === 'string' ? json[key] : ''
  const type = str('type') || 'browser.navigate'
  const step = newStep(type)
  step.id = str('id')
  step.next = str('next')
  step.variableName = str('name')
  step.variableValue = str('value')
  step.conditionExpression = str('expression')
  step.onTrue = str('on_true')
  step.onFalse = str('on_false')
  step.url = str('url')
  step.selector = str('selector')
  step.text = str('text')
  step.seconds =
    typeof json['seconds'] === 'number' ? String(json['seconds']) : '2'
  step.failReason = str('reason')

  const outputs = json['outputs']
  if (type === 'call_flow') {
    step.flowId = str('flow_id')
    step.onChildFailure = str('on_child_failure') || 'fail_parent'
    const inputs = json['inputs']
    if (inputs && typeof inputs === 'object') {
      step.callInputsText =
        formatKeyValueLines(inputs as Record<string, unknown>)
    }
    if (outputs && typeof outputs === 'object') {
      step.callOutputsText =
        formatKeyValueLines(outputs as Record<string, unknown>)
    }
  } else if (outputs && typeof outputs === 'object') {
    step.outputsText = formatKeyValueLines(outputs as Record<string, unknown>)
  }

  step.itemsExpression = str('items')
  step.itemVariable = str('item_variable')
  step.indexVariable = str('index_variable')
  step.bodyStart = str('body_start')
  step.maxIterations =
    typeof json['max_iterations'] === 'number'
      ? String(json['max_iterations'])
      : '1000'
  step.loopCondition = str('condition')

  step.aiInstruction = str('instruction')
  step.aiSchemaJson = str('schema')
  step.aiOutputVariable = str('output_variable')
  const outcomes = json['allowed_outcomes']
  if (Array.isArray(outcomes)) {
    step.allowedOutcomesText = outcomes.join(', ')
  }

  return step
}

const WorkflowsSectionBase = I18nMixin(BaseMixin(PolymerElement)) as {
  new (): PolymerElement & I18nMixinInterface & BaseMixinInterface
}

class WorkflowsSection extends WorkflowsSectionBase {
  static get is() {
    return 'workflows-section'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      workflows_: { type: Array, value: [] },
      showEditDialog_: { type: Boolean, value: false },
      showDeleteDialog_: { type: Boolean, value: false },
      deletingId_: { type: String, value: null },
      editId_: { type: String, value: '' },
      editName_: { type: String, value: '' },
      editVersion_: { type: String, value: '' },
      editSteps_: { type: Array, value: [] },
      saveErrors_: { type: Array, value: [] },
      runResultsById_: { type: Object, value: {} },
      stepTypes_: { type: Array, value: kStepTypes }
    }
  }

  browserProxy_: BraveLeoAssistantBrowserProxy =
    BraveLeoAssistantBrowserProxyImpl.getInstance()
  declare workflows_: WorkflowListItem[]
  declare showEditDialog_: boolean
  declare showDeleteDialog_: boolean
  declare deletingId_: string | null
  declare editId_: string
  declare editName_: string
  declare editVersion_: string
  declare editSteps_: WorkflowStepForm[]
  declare saveErrors_: WorkflowValidationError[]
  declare runResultsById_: Record<string, RunWorkflowResult>
  declare stepTypes_: string[]

  override ready() {
    super.ready()
    this.loadWorkflows_()
  }

  loadWorkflows_() {
    this.browserProxy_.getWorkflows().then((workflows) => {
      this.workflows_ = workflows
    })
  }

  hasWorkflows_(workflows: WorkflowListItem[]): boolean {
    return workflows.length > 0
  }

  hasSaveErrors_(errors: WorkflowValidationError[]): boolean {
    return errors.length > 0
  }

  formatError_(error: WorkflowValidationError): string {
    return error.stepId ? `[${error.stepId}] ${error.message}` : error.message
  }

  hasRunResult_(item: WorkflowListItem): boolean {
    return !!this.runResultsById_[item.id]
  }

  formatRunResult_(item: WorkflowListItem): string {
    const result = this.runResultsById_[item.id]
    if (!result) {
      return ''
    }
    if (result.success) {
      const outputs = Object.entries(result.outputs)
        .map(([k, v]) => `${k}=${v}`)
        .join(', ')
      return `Succeeded after ${result.executedStepIds.length} step(s).` +
        (outputs ? ` Outputs: ${outputs}` : '')
    }
    return `Failed after ${result.executedStepIds.length} step(s): ` +
      result.errorMessage
  }

  // A hint listing every step id currently defined, shown under the step
  // list so the user knows what's available to type into a "next"/
  // "on_true"/"on_false" field - a plain text input for those is far
  // simpler and less error-prone here than trying to keep a live <select>
  // in sync while the referenced step's own id is still being typed.
  knownStepIdsHint_(steps: WorkflowStepForm[]): string {
    const ids = steps.map((s) => s.id).filter((id) => id.length > 0)
    return ids.length > 0 ? `Step ids so far: ${ids.join(', ')}` : ''
  }

  isSelected_(optionValue: string, currentValue: string): boolean {
    return optionValue === currentValue
  }

  isSetVariable_(type: string): boolean { return type === 'set_variable' }
  isCondition_(type: string): boolean { return type === 'condition' }
  isBrowserNavigate_(type: string): boolean { return type === 'browser.navigate' }
  isBrowserClick_(type: string): boolean { return type === 'browser.click' }
  isBrowserType_(type: string): boolean { return type === 'browser.type' }
  isBrowserWait_(type: string): boolean { return type === 'browser.wait' }
  isFail_(type: string): boolean { return type === 'fail' }
  isComplete_(type: string): boolean { return type === 'complete' }
  isCallFlow_(type: string): boolean { return type === 'call_flow' }
  isForEach_(type: string): boolean { return type === 'for_each' }
  isWhileOrUntil_(type: string): boolean {
    return type === 'while' || type === 'until'
  }
  isBreakOrContinue_(type: string): boolean {
    return type === 'break' || type === 'continue'
  }
  isAiExtract_(type: string): boolean { return type === 'ai.extract' }
  isAiDecide_(type: string): boolean { return type === 'ai.decide' }
  needsNext_(type: string): boolean { return stepNeedsNext(type) }

  handleAddNewClick_() {
    this.saveErrors_ = []
    this.editId_ = ''
    this.editName_ = ''
    this.editVersion_ = '1.0.0'
    this.editSteps_ = [newStep('start'), newStep('browser.navigate'),
                      newStep('complete')]
    this.editSteps_[0].next = ''
    this.showEditDialog_ = true
  }

  handleEditClick_(e: { model: { item: WorkflowListItem } }) {
    this.saveErrors_ = []
    try {
      const parsed = JSON.parse(e.model.item.definitionJson)
      this.editId_ = parsed.id || ''
      this.editName_ = parsed.name || ''
      this.editVersion_ = parsed.version || '1.0.0'
      this.editSteps_ = Array.isArray(parsed.steps)
        ? parsed.steps.map(stepFromJson)
        : []
    } catch {
      this.editId_ = e.model.item.id
      this.editName_ = e.model.item.name
      this.editVersion_ = e.model.item.version
      this.editSteps_ = []
    }
    this.showEditDialog_ = true
  }

  handlePublishClick_(e: { model: { item: WorkflowListItem } }) {
    this.browserProxy_.publishWorkflow(e.model.item.id).then(() => {
      this.loadWorkflows_()
    })
  }

  handleRunClick_(e: { model: { item: WorkflowListItem } }) {
    const id = e.model.item.id
    this.browserProxy_.runWorkflow(id).then((result) => {
      this.runResultsById_ = { ...this.runResultsById_, [id]: result }
    })
  }

  handleDeleteClick_(e: { model: { item: WorkflowListItem } }) {
    this.deletingId_ = e.model.item.id
    this.showDeleteDialog_ = true
  }

  onIdInput_(e: { target: HTMLInputElement }) { this.editId_ = e.target.value }
  onNameInput_(e: { target: HTMLInputElement }) {
    this.editName_ = e.target.value
  }
  onVersionInput_(e: { target: HTMLInputElement }) {
    this.editVersion_ = e.target.value
  }

  handleAddStepClick_() {
    this.editSteps_ = [...this.editSteps_, newStep('browser.navigate')]
  }

  handleRemoveStepClick_(e: { model: { index: number } }) {
    const steps = [...this.editSteps_]
    steps.splice(e.model.index, 1)
    this.editSteps_ = steps
  }

  // All the per-field step input handlers follow the same shape: copy the
  // steps array, mutate the one field this input owns on the step at
  // e.model.index, then reassign editSteps_ so Polymer's dom-repeat
  // re-renders - simpler and less error-prone here than fine-grained
  // Polymer path notification given how many fields each step has.
  updateStep_(index: number, mutate: (step: WorkflowStepForm) => void) {
    const steps = [...this.editSteps_]
    const updated = { ...steps[index] }
    mutate(updated)
    steps[index] = updated
    this.editSteps_ = steps
  }

  onStepIdInput_(e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.id = e.target.value })
  }
  onStepTypeChange_(e: { model: { index: number }, target: HTMLSelectElement }) {
    this.updateStep_(e.model.index, (s) => { s.type = e.target.value })
  }
  onStepNextInput_(e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.next = e.target.value })
  }
  onStepVariableNameInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.variableName = e.target.value })
  }
  onStepVariableValueInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.variableValue = e.target.value })
  }
  onStepConditionExpressionInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.conditionExpression = e.target.value })
  }
  onStepOnTrueInput_(e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.onTrue = e.target.value })
  }
  onStepOnFalseInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.onFalse = e.target.value })
  }
  onStepUrlInput_(e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.url = e.target.value })
  }
  onStepSelectorInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.selector = e.target.value })
  }
  onStepTextInput_(e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.text = e.target.value })
  }
  onStepSecondsInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.seconds = e.target.value })
  }
  onStepFailReasonInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.failReason = e.target.value })
  }
  onStepOutputsInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(e.model.index, (s) => { s.outputsText = e.target.value })
  }
  onStepFlowIdInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.flowId = e.target.value })
  }
  onStepCallInputsInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.callInputsText = e.target.value })
  }
  onStepCallOutputsInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.callOutputsText = e.target.value })
  }
  onStepOnChildFailureInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.onChildFailure = e.target.value })
  }
  onStepItemsExpressionInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.itemsExpression = e.target.value })
  }
  onStepItemVariableInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.itemVariable = e.target.value })
  }
  onStepIndexVariableInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.indexVariable = e.target.value })
  }
  onStepBodyStartInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(e.model.index, (s) => { s.bodyStart = e.target.value })
  }
  onStepMaxIterationsInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.maxIterations = e.target.value })
  }
  onStepLoopConditionInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.loopCondition = e.target.value })
  }
  onStepAiInstructionInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.aiInstruction = e.target.value })
  }
  onStepAiSchemaJsonInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.aiSchemaJson = e.target.value })
  }
  onStepAiOutputVariableInput_(
      e: { model: { index: number }, target: HTMLInputElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.aiOutputVariable = e.target.value })
  }
  onStepAllowedOutcomesInput_(
      e: { model: { index: number }, target: HTMLTextAreaElement }) {
    this.updateStep_(
        e.model.index, (s) => { s.allowedOutcomesText = e.target.value })
  }

  onDialogCancel_() {
    this.showEditDialog_ = false
  }

  onDialogSave_() {
    const definition = {
      schema_version: '1.0',
      id: this.editId_.trim(),
      name: this.editName_.trim(),
      version: this.editVersion_.trim() || '1.0.0',
      status: 'draft',
      inputs: {},
      outputs: {},
      variables: {},
      steps: this.editSteps_.map(stepFormToJson)
    }
    this.browserProxy_.saveWorkflow(JSON.stringify(definition)).then(
        (result) => {
          this.saveErrors_ = result.errors
          if (result.id && result.errors.length === 0) {
            this.showEditDialog_ = false
            this.loadWorkflows_()
          }
        })
  }

  onDeleteDialogCancel_() {
    this.showDeleteDialog_ = false
    this.deletingId_ = null
  }

  onDeleteDialogConfirm_() {
    if (this.deletingId_) {
      this.browserProxy_.deleteWorkflow(this.deletingId_).then(() => {
        this.loadWorkflows_()
      })
    }
    this.showDeleteDialog_ = false
    this.deletingId_ = null
  }
}

customElements.define(WorkflowsSection.is, WorkflowsSection)

declare global {
  interface HTMLElementTagNameMap {
    'workflows-section': WorkflowsSection
  }
}

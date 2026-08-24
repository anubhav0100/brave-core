/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import * as React from 'react'
import Button from '@brave/leo/react/button'
import Dialog from '@brave/leo/react/dialog'
import Input from '@brave/leo/react/input'
import TextArea from '@brave/leo/react/textarea'
import Checkbox from '@brave/leo/react/checkbox'
import * as Mojom from '../../../common/mojom'
import { useAIChat } from '../../state/ai_chat_context'
import { dateToMojoTime, mojoTimeToDate, weekdayLabel } from './time_utils'
import styles from './style.module.scss'

interface Props {
  task: Mojom.ScheduledTask | 'new'
  onClose: () => void
}

export default function ScheduledTaskModal(props: Props) {
  const aiChatContext = useAIChat()
  const isNew = props.task === 'new'
  const existing = isNew ? undefined : (props.task as Mojom.ScheduledTask)

  const [name, setName] = React.useState(existing?.name ?? '')
  const [prompt, setPrompt] = React.useState(existing?.prompt ?? '')
  const [recurrence, setRecurrence] = React.useState(
    existing?.recurrence ?? Mojom.ScheduledTaskRecurrence.kDaily,
  )
  const [hour, setHour] = React.useState(existing?.hour ?? 9)
  const [minute, setMinute] = React.useState(existing?.minute ?? 0)
  const [weekdays, setWeekdays] = React.useState<Set<number>>(
    () => new Set(existing?.weekdays ?? []),
  )
  const [oneTimeDate, setOneTimeDate] = React.useState(() => {
    const d = mojoTimeToDate(existing?.oneTimeDate)
    return d ? d.toISOString().slice(0, 10) : ''
  })
  const [toolAllowlist, setToolAllowlist] = React.useState<Set<string>>(
    () => new Set(existing?.toolAllowlist ?? []),
  )

  const availableTools =
    aiChatContext.api.useGetAvailableToolsForScheduling().data ?? []

  const nameError = !name.trim() ? 'Name is required.' : ''
  const promptError = !prompt.trim() ? 'Prompt is required.' : ''
  const dateError =
    recurrence === Mojom.ScheduledTaskRecurrence.kOnce && !oneTimeDate
      ? 'Pick a date for a one-time task.'
      : ''
  const weekdayError =
    recurrence === Mojom.ScheduledTaskRecurrence.kWeekly && weekdays.size === 0
      ? 'Pick at least one weekday.'
      : ''
  const hasErrors = !!(nameError || promptError || dateError || weekdayError)

  const toggleWeekday = (day: number) => {
    setWeekdays((prev) => {
      const next = new Set(prev)
      if (next.has(day)) {
        next.delete(day)
      } else {
        next.add(day)
      }
      return next
    })
  }

  const toggleTool = (toolName: string) => {
    setToolAllowlist((prev) => {
      const next = new Set(prev)
      if (next.has(toolName)) {
        next.delete(toolName)
      } else {
        next.add(toolName)
      }
      return next
    })
  }

  const onSave = () => {
    if (hasErrors) {
      return
    }
    const task: Mojom.ScheduledTask = {
      id: existing?.id ?? '',
      name: name.trim(),
      prompt: prompt.trim(),
      recurrence,
      hour,
      minute,
      weekdays: Array.from(weekdays),
      oneTimeDate: oneTimeDate
        ? dateToMojoTime(new Date(oneTimeDate))
        : undefined,
      toolAllowlist: Array.from(toolAllowlist),
      enabled: existing?.enabled ?? true,
      lastRunTime: existing?.lastRunTime,
      lastRunStatus:
        existing?.lastRunStatus ?? Mojom.ScheduledTaskRunStatus.kNeverRun,
      lastRunSummary: existing?.lastRunSummary ?? '',
      lastConversationUuid: existing?.lastConversationUuid,
      nextRunTime: existing?.nextRunTime,
    }
    if (isNew) {
      aiChatContext.api.service.createScheduledTask(task)
    } else {
      aiChatContext.api.service.updateScheduledTask(task)
    }
    props.onClose()
  }

  const onDelete = () => {
    if (existing) {
      aiChatContext.api.service.deleteScheduledTask(existing.id)
    }
    props.onClose()
  }

  return (
    <Dialog
      isOpen
      showClose
      backdropClickCloses={false}
      onClose={props.onClose}
    >
      <div slot='title'>
        {isNew ? 'New Scheduled Task' : 'Edit Scheduled Task'}
      </div>

      <div className={styles.formSection}>
        <Input
          value={name}
          onInput={(e) => setName(e.value ?? '')}
          showErrors={!!nameError}
        >
          <b>Name</b>
          <div slot='errors'>{nameError}</div>
        </Input>

        <TextArea
          value={prompt}
          onInput={(e) => setPrompt(e.value ?? '')}
          rows={4}
          showErrors={!!promptError}
        >
          <b>Prompt</b>
          <div slot='errors'>{promptError}</div>
        </TextArea>

        <div>
          <span className={styles.fieldLabel}>Runs</span>
          <div className={styles.recurrenceRow}>
            <select
              className={styles.select}
              value={recurrence}
              onChange={(e) => setRecurrence(Number(e.target.value))}
            >
              <option value={Mojom.ScheduledTaskRecurrence.kOnce}>
                Once
              </option>
              <option value={Mojom.ScheduledTaskRecurrence.kDaily}>
                Daily
              </option>
              <option value={Mojom.ScheduledTaskRecurrence.kWeekly}>
                Weekly
              </option>
            </select>
            <span>at</span>
            <input
              className={styles.timeInput}
              type='number'
              min={0}
              max={23}
              value={hour}
              onChange={(e) => setHour(Number(e.target.value))}
            />
            <span>:</span>
            <input
              className={styles.timeInput}
              type='number'
              min={0}
              max={59}
              value={minute}
              onChange={(e) => setMinute(Number(e.target.value))}
            />
          </div>
        </div>

        {recurrence === Mojom.ScheduledTaskRecurrence.kOnce && (
          <Input
            type='date'
            value={oneTimeDate}
            onInput={(e) => setOneTimeDate(e.value ?? '')}
            showErrors={!!dateError}
          >
            <b>Date</b>
            <div slot='errors'>{dateError}</div>
          </Input>
        )}

        {recurrence === Mojom.ScheduledTaskRecurrence.kWeekly && (
          <div>
            <span className={styles.fieldLabel}>Weekdays</span>
            <div className={styles.weekdayRow}>
              {[0, 1, 2, 3, 4, 5, 6].map((day) => (
                <Button
                  key={day}
                  kind={weekdays.has(day) ? 'filled' : 'outline'}
                  size='small'
                  onClick={() => toggleWeekday(day)}
                >
                  {weekdayLabel(day)}
                </Button>
              ))}
            </div>
            {weekdayError && (
              <div className={styles.errorText}>{weekdayError}</div>
            )}
          </div>
        )}

        <div>
          <span className={styles.fieldLabel}>
            Tools allowed to run unattended
          </span>
          <div className={styles.toolsNote}>
            Nobody is present to approve anything while this runs - only the
            tools checked below can be used; any other tool call is refused
            automatically.
          </div>
          <div className={styles.toolsList}>
            {availableTools.map((tool) => (
              <Checkbox
                key={tool.name}
                checked={toolAllowlist.has(tool.name)}
                onChange={() => toggleTool(tool.name)}
              >
                <span title={tool.description}>{tool.name}</span>
              </Checkbox>
            ))}
          </div>
        </div>
      </div>

      <div className={styles.footer}>
        {!isNew ? (
          <Button
            kind='plain'
            className={styles.deleteButton}
            onClick={onDelete}
          >
            Delete
          </Button>
        ) : (
          <div />
        )}
        <div className={styles.rightButtons}>
          <Button
            kind='plain-faint'
            onClick={props.onClose}
          >
            Cancel
          </Button>
          <Button
            kind='filled'
            onClick={onSave}
            isDisabled={hasErrors}
          >
            Save
          </Button>
        </div>
      </div>
    </Dialog>
  )
}

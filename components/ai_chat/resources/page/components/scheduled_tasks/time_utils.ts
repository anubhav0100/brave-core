// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as Mojom from '../../../common/mojom'

// mojo_base.mojom.Time's internalValue is microseconds since the Windows
// FILETIME epoch (1601-01-01 UTC), not the Unix epoch JS Date uses - this is
// the fixed, invariant offset between the two calendar epochs (11644473600
// seconds), not anything specific to this codebase.
const WINDOWS_TO_UNIX_EPOCH_OFFSET_MICROSECONDS = BigInt('11644473600000000')
const MICROSECONDS_PER_MILLISECOND = BigInt(1000)

export function mojoTimeToDate(
  time: { internalValue: bigint } | null | undefined,
): Date | null {
  if (!time) {
    return null
  }
  const microsecondsSinceUnixEpoch =
    time.internalValue - WINDOWS_TO_UNIX_EPOCH_OFFSET_MICROSECONDS
  return new Date(
    Number(microsecondsSinceUnixEpoch / MICROSECONDS_PER_MILLISECOND),
  )
}

export function dateToMojoTime(date: Date): { internalValue: bigint } {
  const microsecondsSinceUnixEpoch =
    BigInt(date.getTime()) * MICROSECONDS_PER_MILLISECOND
  return {
    internalValue:
      microsecondsSinceUnixEpoch + WINDOWS_TO_UNIX_EPOCH_OFFSET_MICROSECONDS,
  }
}

const WEEKDAY_LABELS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']

export function weekdayLabel(day: number): string {
  return WEEKDAY_LABELS[day] ?? '?'
}

function formatDateTime(date: Date): string {
  return date.toLocaleString(undefined, {
    dateStyle: 'medium',
    timeStyle: 'short',
  })
}

function formatTimeOfDay(task: Mojom.ScheduledTask): string {
  return `${String(task.hour).padStart(2, '0')}:${String(task.minute).padStart(2, '0')}`
}

export function formatRecurrence(task: Mojom.ScheduledTask): string {
  const time = formatTimeOfDay(task)
  switch (task.recurrence) {
    case Mojom.ScheduledTaskRecurrence.kDaily:
      return `Daily at ${time}`
    case Mojom.ScheduledTaskRecurrence.kWeekly: {
      const days = task.weekdays.map(weekdayLabel).join(', ')
      return `Weekly (${days || 'no days selected'}) at ${time}`
    }
    case Mojom.ScheduledTaskRecurrence.kOnce:
    default: {
      const date = mojoTimeToDate(task.oneTimeDate)
      return date
        ? `Once on ${date.toLocaleDateString()} at ${time}`
        : `Once at ${time}`
    }
  }
}

export function formatNextRun(task: Mojom.ScheduledTask): string {
  if (!task.enabled) {
    return 'Disabled'
  }
  const next = mojoTimeToDate(task.nextRunTime)
  return next ? `Next run: ${formatDateTime(next)}` : formatRecurrence(task)
}

export function formatLastRun(task: Mojom.ScheduledTask): string | null {
  const last = mojoTimeToDate(task.lastRunTime)
  if (!last) {
    return null
  }
  let statusLabel = 'ran'
  if (task.lastRunStatus === Mojom.ScheduledTaskRunStatus.kSuccess) {
    statusLabel = 'succeeded'
  } else if (task.lastRunStatus === Mojom.ScheduledTaskRunStatus.kFailed) {
    statusLabel = 'failed'
  } else if (task.lastRunStatus === Mojom.ScheduledTaskRunStatus.kPartial) {
    statusLabel = 'stopped early'
  }
  return `Last run ${formatDateTime(last)} (${statusLabel})`
}

/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import * as React from 'react'
import Button from '@brave/leo/react/button'
import Icon from '@brave/leo/react/icon'
import ButtonMenu from '@brave/leo/react/buttonMenu'
import Toggle from '@brave/leo/react/toggle'
import * as Mojom from '../../../common/mojom'
import { useAIChat } from '../../state/ai_chat_context'
import ScheduledTaskModal from './modal'
import { formatLastRun, formatNextRun } from './time_utils'
import styles from './style.module.scss'

interface Props {
  onClose: () => void
}

export default function ScheduledTasksList(props: Props) {
  const aiChatContext = useAIChat()
  const tasks = aiChatContext.api.useGetScheduledTasks().data ?? []
  const [editingTask, setEditingTask] = React.useState<
    Mojom.ScheduledTask | 'new' | null
  >(null)
  const [openMenuId, setOpenMenuId] = React.useState<string>()

  return (
    <div className={styles.scroller}>
      <div className={styles.header}>
        <Button
          kind='plain-faint'
          fab
          onClick={props.onClose}
        >
          <Icon name='arrow-left' />
        </Button>
        <div className={styles.title}>Scheduled Tasks</div>
        <Button
          kind='plain-faint'
          fab
          onClick={() => setEditingTask('new')}
        >
          <Icon name='plus-add' />
        </Button>
      </div>

      {tasks.length === 0 && (
        <div className={styles.empty}>
          No scheduled tasks yet. Ask the AI Assistant to schedule one, or
          create one here.
        </div>
      )}

      <ol className={styles.list}>
        {tasks.map((task) => {
          const lastRun = formatLastRun(task)
          return (
            <li
              key={task.id}
              className={styles.item}
            >
              <div
                className={styles.itemMain}
                onClick={() => setEditingTask(task)}
              >
                <div className={styles.itemName}>{task.name}</div>
                <div className={styles.itemMeta}>{formatNextRun(task)}</div>
                {lastRun && <div className={styles.itemMeta}>{lastRun}</div>}
              </div>
              <Toggle
                checked={task.enabled}
                onChange={({ checked }: { checked: boolean }) =>
                  aiChatContext.api.service.setScheduledTaskEnabled(
                    task.id,
                    checked,
                  )
                }
              />
              <ButtonMenu
                isOpen={openMenuId === task.id}
                onChange={(e: { isOpen: boolean }) =>
                  setOpenMenuId(e.isOpen ? task.id : undefined)
                }
              >
                <Button
                  slot='anchor-content'
                  kind='plain-faint'
                  fab
                  size='small'
                >
                  <Icon name='more-vertical' />
                </Button>
                <leo-menu-item onClick={() => setEditingTask(task)}>
                  <Icon name='edit-pencil' /> Edit
                </leo-menu-item>
                <leo-menu-item
                  onClick={() =>
                    aiChatContext.api.service.deleteScheduledTask(task.id)
                  }
                >
                  <Icon name='trash' /> Delete
                </leo-menu-item>
              </ButtonMenu>
            </li>
          )
        })}
      </ol>

      {editingTask !== null && (
        <ScheduledTaskModal
          task={editingTask}
          onClose={() => setEditingTask(null)}
        />
      )}
    </div>
  )
}

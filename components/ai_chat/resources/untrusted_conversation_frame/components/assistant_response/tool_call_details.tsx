// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { getLocale } from '$web-common/locale'
import * as Mojom from '../../../common/mojom'
import CodeBlock from '../code_block'
import styles from './tool_call_details.module.scss'
import '../../../common/strings'

function formatOutput(output: Mojom.ContentBlock[] | null | undefined) {
  if (!output || output.length === 0) {
    return null
  }
  return output
    .map((block) => {
      if (block.textContentBlock) {
        return block.textContentBlock.text
      }
      // Fall back to a raw dump for block types that don't have a plain-text
      // representation (e.g. image blocks) - this is a debug/trace view, not
      // a rendered preview.
      try {
        return JSON.stringify(block)
      } catch {
        return String(block)
      }
    })
    .join('\n')
}

/**
 * A generic, opt-in-per-click disclosure showing the raw data behind a tool
 * call - tool name, call id, arguments and output - regardless of whether the
 * tool has bespoke rendering elsewhere. This is the "advanced mode" the
 * ToolEvent component's own comment noted as not implemented: useful for
 * understanding exactly what the assistant asked for and got back, including
 * for unrecognized/future tools with no custom UI at all.
 */
export default function ToolCallDetails(props: {
  toolUseEvent: Mojom.ToolUseEvent
}) {
  const [isOpen, setIsOpen] = React.useState(false)
  const { toolUseEvent } = props
  const outputText = React.useMemo(
    () => formatOutput(toolUseEvent.output),
    [toolUseEvent.output],
  )

  return (
    <div className={styles.toolCallDetails}>
      <button
        className={styles.toggle}
        onClick={() => setIsOpen(!isOpen)}
      >
        {isOpen
          ? getLocale(S.CHAT_UI_TOOL_CALL_HIDE_DETAILS)
          : getLocale(S.CHAT_UI_TOOL_CALL_SHOW_DETAILS)}
      </button>
      {isOpen && (
        <div className={styles.content}>
          <div className={styles.field}>
            <span className={styles.fieldLabel}>
              {getLocale(S.CHAT_UI_TOOL_CALL_NAME_LABEL)}
            </span>{' '}
            {toolUseEvent.toolName}
          </div>
          <div className={styles.field}>
            <span className={styles.fieldLabel}>
              {getLocale(S.CHAT_UI_TOOL_CALL_ID_LABEL)}
            </span>{' '}
            {toolUseEvent.id}
          </div>
          {!!toolUseEvent.argumentsJson && (
            <div className={styles.field}>
              <div className={styles.fieldLabel}>
                {getLocale(S.CHAT_UI_TOOL_CALL_ARGUMENTS_LABEL)}
              </div>
              <CodeBlock.Block
                code={toolUseEvent.argumentsJson}
                lang='json'
              />
            </div>
          )}
          {!!outputText && (
            <div className={styles.field}>
              <div className={styles.fieldLabel}>
                {getLocale(S.CHAT_UI_TOOL_CALL_OUTPUT_LABEL)}
              </div>
              <CodeBlock.Block
                code={outputText}
                lang='text'
              />
            </div>
          )}
        </div>
      )}
    </div>
  )
}

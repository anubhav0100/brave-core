// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as ComputerUseMojo from 'gen/brave/components/computer_use/common/computer_use_ui.mojom.m.js'
import * as React from 'react'
import styled from 'styled-components'
import { createRoot } from 'react-dom/client'
import StyledComponentsProvider from '$web-common/StyledComponentsProvider'

const API = ComputerUseMojo.PageHandler.getRemote()

const Container = styled.div`
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 16px;
  font-family: sans-serif;
`

const Banner = styled.div<{ active: boolean }>`
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 16px;
  border-radius: 8px;
  font-weight: 600;
  background: ${p => (p.active ? '#fef2d2' : '#eee')};
  color: ${p => (p.active ? '#7a5b00' : '#666')};
`

const Dot = styled.span<{ active: boolean }>`
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: ${p => (p.active ? '#e8a400' : '#999')};
`

const ButtonRow = styled.div`
  display: flex;
  gap: 8px;
`

const FrameContainer = styled.div`
  border: 1px solid #ddd;
  border-radius: 8px;
  padding: 8px;
  min-height: 200px;
  display: flex;
  align-items: center;
  justify-content: center;
`

const Frame = styled.img`
  max-width: 100%;
  max-height: 80vh;
`

const EmptyState = styled.div`
  color: #888;
`

function App() {
  const [active, setActive] = React.useState(false)
  const [frameDataUrl, setFrameDataUrl] = React.useState('')

  const refresh = React.useCallback(() => {
    API.getState().then((r: { active: boolean, frameDataUrl: string }) => {
      setActive(r.active)
      setFrameDataUrl(r.frameDataUrl)
    })
  }, [])

  React.useEffect(() => {
    refresh()
  }, [refresh])

  const stop = () => {
    API.stop()
    setActive(false)
  }

  return (
    <Container>
      <Banner active={active}>
        <Dot active={active} />
        {active
          ? 'AI is viewing this desktop'
          : 'No active AI computer-use session'}
      </Banner>
      <ButtonRow>
        <button onClick={refresh}>Refresh</button>
        <button onClick={stop} disabled={!active}>Stop</button>
      </ButtonRow>
      <FrameContainer>
        {frameDataUrl
          ? <Frame src={frameDataUrl} alt="Latest captured desktop frame" />
          : <EmptyState>No screenshot captured yet in this session.</EmptyState>}
      </FrameContainer>
    </Container>
  )
}

document.addEventListener('DOMContentLoaded', () => {
  const root = createRoot(document.getElementById('root')!)
  root.render(
    <StyledComponentsProvider>
      <App />
    </StyledComponentsProvider>
  )
})

// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as ComputerUseMojo from 'gen/brave/components/computer_use/common/computer_use_ui.mojom.m.js'
import * as React from 'react'
import styled, { createGlobalStyle, keyframes } from 'styled-components'
import { createRoot } from 'react-dom/client'
import StyledComponentsProvider from '$web-common/StyledComponentsProvider'

const API = ComputerUseMojo.PageHandler.getRemote()

type Status = 'idle' | 'active' | 'stopped'

const statusColors: Record<Status, { from: string, to: string, glow: string, text: string }> = {
  idle: { from: '#3b3f8f', to: '#2a2d66', glow: '#5b5fc7', text: '#c9caf7' },
  active: { from: '#ff8a00', to: '#e63946', glow: '#ff8a00', text: '#fff3e0' },
  stopped: { from: '#d90429', to: '#7a0d18', glow: '#ff3355', text: '#ffe3e6' },
}

const pulse = keyframes`
  0%, 100% { box-shadow: 0 0 0px 0px rgba(255, 138, 0, 0.6); }
  50% { box-shadow: 0 0 18px 4px rgba(255, 138, 0, 0.55); }
`

const GlobalStyle = createGlobalStyle`
  body {
    margin: 0;
    background: radial-gradient(circle at 20% 20%, #1b1e3d 0%, #0d0e21 60%, #08091a 100%);
    min-height: 100vh;
  }
`

const Container = styled.div`
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 28px;
  max-width: 900px;
  margin: 0 auto;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  color: #e8e9f7;
`

const Header = styled.h1`
  margin: 0;
  font-size: 22px;
  font-weight: 700;
  background: linear-gradient(90deg, #ff8a00, #e63946, #7b5bff);
  background-clip: text;
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
`

const Banner = styled.div<{ $status: Status }>`
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 20px;
  border-radius: 14px;
  font-weight: 600;
  font-size: 15px;
  color: ${p => statusColors[p.$status].text};
  background: linear-gradient(120deg, ${p => statusColors[p.$status].from}, ${p => statusColors[p.$status].to});
  ${p => p.$status === 'active' ? `animation: ${pulse} 2s ease-in-out infinite;` : ''}
`

const Dot = styled.span<{ $status: Status }>`
  width: 12px;
  height: 12px;
  border-radius: 50%;
  flex-shrink: 0;
  background: ${p => statusColors[p.$status].glow};
  box-shadow: 0 0 8px 2px ${p => statusColors[p.$status].glow};
`

const ButtonRow = styled.div`
  display: flex;
  gap: 10px;
`

const buttonBase = `
  border: none;
  border-radius: 10px;
  padding: 10px 18px;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: transform 0.1s ease, opacity 0.15s ease;
  color: white;

  &:hover:not(:disabled) {
    transform: translateY(-1px);
  }

  &:disabled {
    opacity: 0.35;
    cursor: default;
  }
`

const RefreshButton = styled.button`
  ${buttonBase}
  background: linear-gradient(120deg, #4361ee, #3a0ca3);
`

const StopButton = styled.button`
  ${buttonBase}
  background: linear-gradient(120deg, #ff4d6d, #c9184a);
`

const ResumeButton = styled.button`
  ${buttonBase}
  background: linear-gradient(120deg, #38b000, #007f5f);
`

const RdpBadge = styled.div`
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 16px;
  border-radius: 12px;
  font-size: 13px;
  font-weight: 600;
  color: #e6e0ff;
  background: linear-gradient(120deg, #4834d4, #2d1b69);
  border: 1px solid #7b5bff55;
`

const FrameContainer = styled.div<{ $status: Status }>`
  border: 2px solid ${p => statusColors[p.$status].glow}55;
  border-radius: 16px;
  padding: 10px;
  min-height: 240px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(255, 255, 255, 0.03);
  box-shadow: inset 0 0 40px rgba(0, 0, 0, 0.35);
`

const Frame = styled.img`
  max-width: 100%;
  max-height: 75vh;
  border-radius: 10px;
`

const EmptyState = styled.div`
  color: #8688b0;
  font-size: 14px;
`

const Footer = styled.div`
  font-size: 12px;
  color: #7375a3;
  line-height: 1.6;
`

const Kbd = styled.span`
  display: inline-block;
  padding: 1px 7px;
  border-radius: 5px;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.15);
  font-family: monospace;
  color: #c9caf7;
`

function bannerText(status: Status): string {
  if (status === 'stopped') {
    return 'Emergency stop active - AI desktop input is blocked'
  }
  if (status === 'active') {
    return 'AI is viewing / controlling this desktop'
  }
  return 'No active AI computer-use session'
}

function App() {
  const [active, setActive] = React.useState(false)
  const [emergencyStopped, setEmergencyStopped] = React.useState(false)
  const [frameDataUrl, setFrameDataUrl] = React.useState('')
  const [rdpActive, setRdpActive] = React.useState(false)
  const [rdpTargetHost, setRdpTargetHost] = React.useState('')

  const refresh = React.useCallback(() => {
    API.getState().then(
      (r: {
        active: boolean
        emergencyStopped: boolean
        frameDataUrl: string
        rdpActive: boolean
        rdpTargetHost: string
      }) => {
        setActive(r.active)
        setEmergencyStopped(r.emergencyStopped)
        setFrameDataUrl(r.frameDataUrl)
        setRdpActive(r.rdpActive)
        setRdpTargetHost(r.rdpTargetHost)
      }
    )
  }, [])

  React.useEffect(() => {
    refresh()
  }, [refresh])

  const stop = () => {
    API.stop()
    setEmergencyStopped(true)
    setActive(false)
  }

  const resume = () => {
    API.resume()
    setEmergencyStopped(false)
  }

  const status: Status = emergencyStopped ? 'stopped' : active ? 'active' : 'idle'

  return (
    <Container>
      <Header>AI Computer Use</Header>
      <Banner $status={status}>
        <Dot $status={status} />
        {bannerText(status)}
      </Banner>
      {rdpActive && (
        <RdpBadge>
          Connected via RDP to <strong>{rdpTargetHost}</strong> - a separate
          window shows the remote desktop.
        </RdpBadge>
      )}
      <ButtonRow>
        <RefreshButton onClick={refresh}>Refresh</RefreshButton>
        <StopButton onClick={stop} disabled={emergencyStopped}>
          Stop
        </StopButton>
        {emergencyStopped && (
          <ResumeButton onClick={resume}>Resume</ResumeButton>
        )}
      </ButtonRow>
      <FrameContainer $status={status}>
        {frameDataUrl
          ? <Frame src={frameDataUrl} alt="Latest captured desktop frame" />
          : <EmptyState>No screenshot captured yet in this session.</EmptyState>}
      </FrameContainer>
      <Footer>
        Global emergency stop: <Kbd>Ctrl</Kbd> + <Kbd>Alt</Kbd> + <Kbd>Shift</Kbd> + <Kbd>Esc</Kbd> - works even when the browser isn't focused.
      </Footer>
    </Container>
  )
}

document.addEventListener('DOMContentLoaded', () => {
  const root = createRoot(document.getElementById('root')!)
  root.render(
    <StyledComponentsProvider>
      <GlobalStyle />
      <App />
    </StyledComponentsProvider>
  )
})

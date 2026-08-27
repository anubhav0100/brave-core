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
const pageCallbackRouter = new ComputerUseMojo.PageCallbackRouter()
API.bindPage(pageCallbackRouter.$.bindNewPipeAndPassRemote())

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
  }
`

const Container = styled.div`
  /* Set directly on this scoped component rather than on body via
     GlobalStyle - the body-targeted background rule wasn't reliably taking
     effect on this WebUI page (the page rendered on a plain white
     background instead), which made every light/pastel text color below
     nearly invisible. This uses the same styled-components mechanism the
     Header/Banner/buttons already render correctly with. */
  min-height: 100vh;
  background: radial-gradient(circle at 20% 20%, #1b1e3d 0%, #0d0e21 60%, #08091a 100%);
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 28px;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  color: #e8e9f7;
`

const Inner = styled.div`
  display: flex;
  flex-direction: column;
  gap: 20px;
  max-width: 900px;
  width: 100%;
  margin: 0 auto;
`

const Header = styled.h1`
  margin: 0;
  font-size: 22px;
  font-weight: 700;
  /* A solid, guaranteed-visible color rather than a gradient text-clip -
     that trick renders invisible (transparent, uncolored text) if
     background-clip: text fails to apply for any reason, which is a real
     risk on a WebUI page rather than a normal web page. */
  color: #ffb15c;
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

const Section = styled.div`
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 18px 20px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(255, 255, 255, 0.08);
`

const SectionTitle = styled.h2`
  margin: 0;
  font-size: 14px;
  font-weight: 700;
  color: #c9caf7;
  text-transform: uppercase;
  letter-spacing: 0.06em;
`

const RdpForm = styled.div`
  display: flex;
  gap: 10px;
  align-items: center;
  flex-wrap: wrap;
`

const inputBase = `
  border-radius: 8px;
  border: 1px solid rgba(255, 255, 255, 0.15);
  background: rgba(255, 255, 255, 0.05);
  color: #e8e9f7;
  padding: 9px 12px;
  font-size: 14px;

  &::placeholder {
    color: #9294c0;
  }

  &:focus {
    outline: none;
    border-color: #7b5bff;
  }

  &:disabled {
    opacity: 0.5;
  }
`

const HostInput = styled.input`
  ${inputBase}
  flex: 1;
  min-width: 200px;
`

const PortInput = styled.input`
  ${inputBase}
  width: 90px;
`

const ConnectButton = styled.button`
  ${buttonBase}
  background: linear-gradient(120deg, #7b5bff, #4834d4);
`

const RdpErrorText = styled.div`
  color: #ff8fa3;
  font-size: 13px;
`

const RdpCanvasContainer = styled.div`
  border: 2px solid #7b5bff55;
  border-radius: 14px;
  padding: 8px;
  display: flex;
  justify-content: center;
  background: #000;
`

const RdpCanvas = styled.canvas`
  max-width: 100%;
  max-height: 75vh;
  border-radius: 8px;
  cursor: default;
  outline: none;
`

const HistoryTable = styled.table`
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;

  th {
    text-align: left;
    padding: 8px 10px;
    color: #aeb0d6;
    font-weight: 600;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  }

  td {
    padding: 8px 10px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    color: #e8e9f7;
  }
`

const OpenBadge = styled.span`
  color: #7cf29c;
  font-weight: 600;
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
  color: #aeb0d6;
  font-size: 14px;
`

const ToggleRow = styled.label`
  display: flex;
  align-items: flex-start;
  gap: 10px;
  cursor: pointer;
  font-size: 14px;
`

const ToggleCheckbox = styled.input`
  margin-top: 2px;
  width: 16px;
  height: 16px;
  flex-shrink: 0;
  accent-color: #7b5bff;
`

const ToggleText = styled.div`
  display: flex;
  flex-direction: column;
  gap: 2px;
`

const ToggleDesc = styled.span`
  font-size: 12px;
  color: #aeb0d6;
`

const Footer = styled.div`
  font-size: 12px;
  color: #9294c0;
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

interface RdpHistoryEntry {
  host: string
  port: number
  connectedAt: number
  disconnectedAt: number | null
}

function formatTimestamp(ms: number): string {
  return new Date(ms).toLocaleString()
}

function formatDuration(connectedAt: number, disconnectedAt: number | null): string {
  const endMs = disconnectedAt ?? Date.now()
  const totalSeconds = Math.max(0, Math.round((endMs - connectedAt) / 1000))
  const hours = Math.floor(totalSeconds / 3600)
  const minutes = Math.floor((totalSeconds % 3600) / 60)
  const seconds = totalSeconds % 60
  const parts = []
  if (hours > 0) parts.push(`${hours}h`)
  if (hours > 0 || minutes > 0) parts.push(`${minutes}m`)
  parts.push(`${seconds}s`)
  return parts.join(' ')
}

function App() {
  const [active, setActive] = React.useState(false)
  const [emergencyStopped, setEmergencyStopped] = React.useState(false)
  const [frameDataUrl, setFrameDataUrl] = React.useState('')
  const [rdpActive, setRdpActive] = React.useState(false)
  const [rdpTargetHost, setRdpTargetHost] = React.useState('')
  const [rdpTargetPort, setRdpTargetPort] = React.useState(0)
  const [rdpHostInput, setRdpHostInput] = React.useState('')
  const [rdpPortInput, setRdpPortInput] = React.useState('3389')
  const [rdpError, setRdpError] = React.useState('')
  const [rdpConnecting, setRdpConnecting] = React.useState(false)
  const [rdpHistory, setRdpHistory] = React.useState<RdpHistoryEntry[]>([])
  const [alwaysAllowScreenshot, setAlwaysAllowScreenshot] = React.useState(false)
  const rdpCanvasRef = React.useRef<HTMLCanvasElement>(null)

  const refreshAlwaysAllowScreenshot = React.useCallback(() => {
    API.getAlwaysAllowDesktopScreenshot().then(
      (r: { alwaysAllow: boolean }) => {
        setAlwaysAllowScreenshot(r.alwaysAllow)
      }
    )
  }, [])

  const toggleAlwaysAllowScreenshot = () => {
    const next = !alwaysAllowScreenshot
    setAlwaysAllowScreenshot(next)
    API.setAlwaysAllowDesktopScreenshot(next)
  }

  const refresh = React.useCallback(() => {
    API.getState().then(
      (r: {
        active: boolean
        emergencyStopped: boolean
        frameDataUrl: string
        rdpActive: boolean
        rdpTargetHost: string
        rdpTargetPort: number
      }) => {
        setActive(r.active)
        setEmergencyStopped(r.emergencyStopped)
        setFrameDataUrl(r.frameDataUrl)
        setRdpActive(r.rdpActive)
        setRdpTargetHost(r.rdpTargetHost)
        setRdpTargetPort(r.rdpTargetPort)
      }
    )
  }, [])

  const refreshRdpHistory = React.useCallback(() => {
    API.getRdpHistory().then((r: { history: Array<{
      host: string
      port: number
      connectedAt: number
      disconnectedAt?: number
    }> }) => {
      setRdpHistory(r.history.map(h => ({
        host: h.host,
        port: h.port,
        connectedAt: h.connectedAt,
        disconnectedAt: h.disconnectedAt ?? null,
      })))
    })
  }, [])

  React.useEffect(() => {
    refresh()
    refreshRdpHistory()
    refreshAlwaysAllowScreenshot()
  }, [refresh, refreshRdpHistory, refreshAlwaysAllowScreenshot])

  // Live RDP view: pushed by ComputerUseUI's Page interface (see
  // computer_use_ui.mojom) - a fresh frame roughly every 200ms while an RDP
  // session is connected, and a state update whenever RDP connects or
  // disconnects for any reason (this page's own button, the AI's tool, or
  // the remote end). This page is never torn down and recreated (a full
  // navigation reloads the whole document), so these listeners are
  // registered once for the page's lifetime rather than cleaned up.
  React.useEffect(() => {
    pageCallbackRouter.onFrameCaptured.addListener((frameDataUrl: string) => {
      const canvas = rdpCanvasRef.current
      if (!canvas) {
        return
      }
      const image = new Image()
      image.onload = () => {
        if (canvas.width !== image.naturalWidth || canvas.height !== image.naturalHeight) {
          canvas.width = image.naturalWidth
          canvas.height = image.naturalHeight
        }
        canvas.getContext('2d')?.drawImage(image, 0, 0)
      }
      image.src = frameDataUrl
    })
    pageCallbackRouter.onRdpStateChanged.addListener(
      (active: boolean, host: string, port: number) => {
        setRdpActive(active)
        setRdpTargetHost(host)
        setRdpTargetPort(port)
        refreshRdpHistory()
      }
    )
  }, [refreshRdpHistory])

  // Translates a canvas-local pointer event into RDP-window client-area
  // coordinates - the canvas' backing store (width/height) is kept sized to
  // exactly match the captured frame's native resolution (see
  // onFrameCaptured above), while its on-page display size is independently
  // scaled by CSS (max-width/max-height), so this scales back out to native
  // pixels before forwarding.
  const toRdpCoords = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = rdpCanvasRef.current!
    const rect = canvas.getBoundingClientRect()
    const scaleX = canvas.width / rect.width
    const scaleY = canvas.height / rect.height
    return {
      x: Math.round((e.clientX - rect.left) * scaleX),
      y: Math.round((e.clientY - rect.top) * scaleY),
    }
  }

  const handleRdpMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const { x, y } = toRdpCoords(e)
    API.sendRdpMouseEvent(x, y, e.buttons, 0)
  }

  const handleRdpMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    e.currentTarget.focus()
    const { x, y } = toRdpCoords(e)
    API.sendRdpMouseEvent(x, y, e.buttons, 0)
  }

  const handleRdpMouseUp = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const { x, y } = toRdpCoords(e)
    API.sendRdpMouseEvent(x, y, e.buttons, 0)
  }

  const handleRdpWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    e.preventDefault()
    const { x, y } = toRdpCoords(e)
    // Legacy WheelEvent.wheelDelta convention (positive = up, multiples of
    // 120) - see RdpSession::SendMouseEvent's doc comment for why this,
    // rather than deltaY, is the wire format.
    const wheelDelta = e.deltaY < 0 ? 120 : -120
    API.sendRdpMouseEvent(x, y, e.buttons, wheelDelta)
  }

  const handleRdpKeyDown = (e: React.KeyboardEvent<HTMLCanvasElement>) => {
    e.preventDefault()
    API.sendRdpKeyEvent(e.keyCode, true)
  }

  const handleRdpKeyUp = (e: React.KeyboardEvent<HTMLCanvasElement>) => {
    e.preventDefault()
    API.sendRdpKeyEvent(e.keyCode, false)
  }

  const stop = () => {
    API.stop()
    setEmergencyStopped(true)
    setActive(false)
  }

  const resume = () => {
    API.resume()
    setEmergencyStopped(false)
  }

  const connectRdp = () => {
    const port = parseInt(rdpPortInput, 10)
    if (!rdpHostInput.trim()) {
      setRdpError('Enter a host to connect to.')
      return
    }
    if (!Number.isFinite(port) || port <= 0 || port > 65535) {
      setRdpError('Enter a valid port (1-65535).')
      return
    }
    setRdpError('')
    setRdpConnecting(true)
    API.connectRdp(rdpHostInput.trim(), port).then(
      (r: { success: boolean, error: string }) => {
        setRdpConnecting(false)
        if (!r.success) {
          setRdpError(r.error)
          return
        }
        refresh()
        refreshRdpHistory()
      }
    )
  }

  const disconnectRdp = () => {
    API.disconnectRdp()
    // The disconnect happens asynchronously on the native side (the RDP
    // window closes itself) - poll state/history shortly after so this
    // page reflects it without the user having to hit Refresh.
    setTimeout(() => {
      refresh()
      refreshRdpHistory()
    }, 500)
  }

  const status: Status = emergencyStopped ? 'stopped' : active ? 'active' : 'idle'

  return (
    <Container>
    <Inner>
      <Header>AI Computer Use</Header>
      <Banner $status={status}>
        <Dot $status={status} />
        {bannerText(status)}
      </Banner>
      {rdpActive && (
        <RdpBadge>
          Connected via RDP to <strong>{rdpTargetHost}:{rdpTargetPort}</strong> -
          shown live in the Remote Desktop section below.
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

      <Section>
        <SectionTitle>Settings</SectionTitle>
        <ToggleRow>
          <ToggleCheckbox
            type="checkbox"
            checked={alwaysAllowScreenshot}
            onChange={toggleAlwaysAllowScreenshot}
          />
          <ToggleText>
            <span>Always allow AI screenshot access</span>
            <ToggleDesc>
              Skips the "Security warning" permission prompt in every new
              conversation. Off by default - the AI Assistant will keep
              asking once per conversation unless you turn this on.
            </ToggleDesc>
          </ToggleText>
        </ToggleRow>
      </Section>

      <Section>
        <SectionTitle>Remote Desktop (RDP)</SectionTitle>
        {rdpActive ? (
          <>
            <RdpForm>
              <span>Connected to {rdpTargetHost}:{rdpTargetPort}</span>
              <StopButton onClick={disconnectRdp}>Disconnect</StopButton>
            </RdpForm>
            <RdpCanvasContainer>
              <RdpCanvas
                ref={rdpCanvasRef}
                tabIndex={0}
                onMouseMove={handleRdpMouseMove}
                onMouseDown={handleRdpMouseDown}
                onMouseUp={handleRdpMouseUp}
                onWheel={handleRdpWheel}
                onKeyDown={handleRdpKeyDown}
                onKeyUp={handleRdpKeyUp}
                onContextMenu={e => e.preventDefault()}
              />
            </RdpCanvasContainer>
          </>
        ) : (
          <RdpForm>
            <HostInput
              placeholder="Host or IP address"
              value={rdpHostInput}
              disabled={rdpConnecting}
              onChange={e => setRdpHostInput(e.target.value)}
            />
            <PortInput
              placeholder="Port"
              value={rdpPortInput}
              disabled={rdpConnecting}
              onChange={e => setRdpPortInput(e.target.value)}
            />
            <ConnectButton onClick={connectRdp} disabled={rdpConnecting}>
              {rdpConnecting ? 'Connecting...' : 'Connect'}
            </ConnectButton>
          </RdpForm>
        )}
        {rdpError && <RdpErrorText>{rdpError}</RdpErrorText>}
      </Section>

      <Section>
        <SectionTitle>RDP Session History</SectionTitle>
        {rdpHistory.length === 0 ? (
          <EmptyState>No RDP sessions on this profile yet.</EmptyState>
        ) : (
          <HistoryTable>
            <thead>
              <tr>
                <th>Host:Port</th>
                <th>Connected</th>
                <th>Disconnected</th>
                <th>Duration</th>
              </tr>
            </thead>
            <tbody>
              {rdpHistory.map((h, i) => (
                <tr key={i}>
                  <td>{h.host}:{h.port}</td>
                  <td>{formatTimestamp(h.connectedAt)}</td>
                  <td>
                    {h.disconnectedAt
                      ? formatTimestamp(h.disconnectedAt)
                      : <OpenBadge>Still connected</OpenBadge>}
                  </td>
                  <td>{formatDuration(h.connectedAt, h.disconnectedAt)}</td>
                </tr>
              ))}
            </tbody>
          </HistoryTable>
        )}
      </Section>

      <Footer>
        Global emergency stop: <Kbd>Ctrl</Kbd> + <Kbd>Alt</Kbd> + <Kbd>Shift</Kbd> + <Kbd>Esc</Kbd> - works even when the browser isn't focused.
      </Footer>
    </Inner>
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

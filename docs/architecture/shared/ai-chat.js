/**
 * Horo AI Chat — shared side panel
 *
 * Include in editor pages alongside ai-chat.css.
 * Automatically appends the chat panel to <body> and injects a button
 * into every .activity-bar-right element on the page.
 *
 * Usage:
 *   <link rel="stylesheet" href="../shared/ai-chat.css">
 *   <script src="../shared/ai-chat.js"></script>
 *
 * Public API:
 *   toggleAiChat()          — open / close the panel
 *   sendAiChatMessage()     — submit the current input (static demo)
 *   toggleAiChatHistory()   — open / close the session history overlay
 *   newAiChatSession()      — start a fresh chat, archiving the current one
 */
;(() => {
  'use strict'

  /* ── Static config (mirrors patterns from Cursor / Copilot Chat) ── */

  const MODES = [
    { id: 'ask', label: 'Ask', placeholder: 'Ask Horo…' },
    { id: 'agent', label: 'Agent', placeholder: 'Tell the agent what to build…' },
    { id: 'edit', label: 'Edit', placeholder: 'Describe the edit to make…' },
  ]

  const MODELS = [
    { id: 'opus-4.8', name: 'Opus 4.8', desc: 'Most capable · slower', group: 'Anthropic' },
    { id: 'sonnet-5', name: 'Sonnet 5', desc: 'Balanced speed & quality', group: 'Anthropic' },
    { id: 'haiku-4.5', name: 'Haiku 4.5', desc: 'Fastest · lightweight', group: 'Anthropic' },
  ]

  const AGENTS = [
    { id: 'horo', name: 'Horo', desc: 'General engine & scene helper' },
    { id: 'shader', name: 'Shader Agent', desc: 'GLSL / material pipeline' },
    { id: 'anim', name: 'Animation Agent', desc: 'Rigging, timelines, blend trees' },
    { id: 'debug', name: 'Debug Agent', desc: 'Stack traces & runtime errors' },
  ]

  const CONTEXT_ITEMS = [
    { id: 'file', label: 'Current file' },
    { id: 'selection', label: 'Selection' },
    { id: 'tabs', label: 'Open tabs' },
    { id: 'image', label: 'Image…' },
  ]

  /* ── Runtime state ─────────────────────────────────────────────── */

  const state = {
    mode: 'ask',
    modelId: 'opus-4.8',
    agentId: 'horo',
    currentSessionId: null,
    historyOpen: false,
    contextChips: [],
    sessions: seedSessions(),
  }

  function seedSessions() {
    const now = Date.now()
    const hr = 3600 * 1000
    return [
      {
        id: 's1',
        title: 'Fix camera clipping in canyon scene',
        updated: now - 2 * hr,
        messages: [
          { role: 'user', text: 'Camera clips through rock geometry near the canyon checkpoint.' },
          { role: 'agent', text: 'Likely the near-clip plane is too large for that corridor width. Try dropping it to 0.05 on the player camera component.' },
        ],
      },
      {
        id: 's2',
        title: 'Blend tree for sprint-to-walk transition',
        updated: now - 26 * hr,
        messages: [
          { role: 'user', text: 'How do I smooth the sprint to walk transition without foot sliding?' },
          { role: 'agent', text: 'Add an intermediate jog state and drive the blend with normalized speed rather than a boolean, so the transition time scales with velocity.' },
        ],
      },
      {
        id: 's3',
        title: 'Shader: rim light flicker on foliage',
        updated: now - 4 * 24 * hr,
        messages: [
          { role: 'user', text: 'Foliage rim lighting flickers when the camera rotates.' },
          { role: 'agent', text: 'That is usually normal-based rim lighting fighting with vertex normals on double-sided leaves. Switch to a view-independent fresnel term using the geometric normal.' },
        ],
      },
    ]
  }

  /* ── Panel DOM ───────────────────────────────── */

  function ensurePanel() {
    let panel = document.getElementById('ai-chat-panel')
    if (panel) return panel

    panel = document.createElement('div')
    panel.id = 'ai-chat-panel'
    panel.className = 'ai-chat-panel'
    panel.innerHTML = `
      <div class="ai-chat-header">
        <button class="ai-chat-icon-btn" data-action="toggle-history" title="Chat history">${ICON_HISTORY}</button>
        <span class="ai-chat-title">Horo AI Chat</span>
        <div class="ai-chat-header-actions">
          <button class="ai-chat-icon-btn" data-action="new-chat" title="New chat">${ICON_PLUS}</button>
          <button class="ai-chat-icon-btn ai-chat-close" onclick="toggleAiChat()" aria-label="Close chat">&times;</button>
        </div>
      </div>

      <div class="ai-chat-history" id="ai-chat-history">
        <div class="ai-chat-msg agent">
          <div class="ai-chat-role">Horo</div>
          <div class="ai-chat-text">Ready. Ask me about the engine, scene editing, or any workflow.</div>
        </div>
      </div>

      <div class="ai-chat-history-panel" id="ai-chat-history-panel">
        <div class="history-panel-head">
          <input id="ai-chat-history-search" placeholder="Search chats…" autocomplete="off">
          <button class="history-new-btn" data-action="new-chat">+ New chat</button>
        </div>
        <div class="history-panel-list" id="ai-chat-history-list"></div>
      </div>

      <div class="ai-chat-input-wrap">
        <div class="ai-chat-composer">
          <div class="composer-context-row" id="ai-chat-context-row"></div>

          <textarea id="ai-chat-input" placeholder="Ask Horo…" rows="1"
            onkeydown="if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();sendAiChatMessage()}"></textarea>

          <div class="composer-footer">
            <div class="composer-footer-left">
              <div class="ai-chat-select model-select-inline" id="ai-chat-model-select" data-type="model">
                <button class="select-trigger-inline" data-action="toggle-select" data-type="model">
                  <span class="select-label" id="ai-chat-model-label"></span>
                  ${ICON_CHEVRON}
                </button>
                <div class="ai-chat-select-menu up" id="ai-chat-model-menu"></div>
              </div>
              <span class="composer-dot">·</span>
              <div class="ai-chat-select agent-select-inline" id="ai-chat-agent-select" data-type="agent">
                <button class="select-trigger-inline" data-action="toggle-select" data-type="agent">
                  <span class="select-label" id="ai-chat-agent-label"></span>
                  ${ICON_CHEVRON}
                </button>
                <div class="ai-chat-select-menu up" id="ai-chat-agent-menu"></div>
              </div>
            </div>
            <div class="composer-footer-right">
              <div class="mode-toggle-text" id="ai-chat-mode-toggle"></div>
              <button class="composer-submit" onclick="sendAiChatMessage()" title="Send (Enter)">
                Send ${ICON_SEND}
              </button>
            </div>
          </div>
        </div>

        <button class="composer-prev-link" data-action="toggle-history">
          Previous chats ${ICON_CHEVRON_RIGHT}
        </button>
      </div>`
    document.body.appendChild(panel)

    renderModeToggle()
    renderSelectMenu('agent')
    renderSelectMenu('model')
    renderContextRow()
    renderHistoryList()
    bindPanelEvents(panel)
    syncPanelBottom()
    return panel
  }

  /* ── Icons ──────────────────────────────────── */

  const ICON_HISTORY =
    '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M8 4v4l2.5 1.5"/><path d="M2.5 6.5A5.5 5.5 0 118 13.5a5.5 5.5 0 01-4.6-2.5"/><path d="M2.5 3.5v3h3"/>' +
    '</svg>'

  const ICON_PLUS =
    '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round">' +
    '<path d="M8 3v10M3 8h10"/></svg>'

  const ICON_CHEVRON =
    '<svg class="chevron" viewBox="0 0 10 6" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M1 1l4 4 4-4"/></svg>'

  const ICON_CHECK =
    '<svg class="check" viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M2 6.5l2.5 2.5L10 3"/></svg>'

  const ICON_TRASH =
    '<svg viewBox="0 0 14 14" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M2.5 4h9M5.5 4V2.7c0-.4.3-.7.7-.7h1.6c.4 0 .7.3.7.7V4M4 4l.5 8c0 .5.4.9.9.9h3.2c.5 0 .9-.4.9-.9L10 4"/>' +
    '</svg>'

  const ICON_SEND =
    '<svg viewBox="0 0 16 16" fill="currentColor"><path d="M1 1l14 7-14 7 3.5-7z"/></svg>'

  const ICON_CHEVRON_RIGHT =
    '<svg viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M3 1.5l4 3.5-4 3.5"/></svg>'

  const ICON_X_SMALL =
    '<svg viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round">' +
    '<path d="M2 2l6 6M8 2l-6 6"/></svg>'

  /* ── Mode toggle ("ask / agent / edit" text switch) ──────────── */

  function renderModeToggle() {
    const el = document.getElementById('ai-chat-mode-toggle')
    if (!el) return
    el.innerHTML = MODES.map((m, i) => {
      const btn = `<button type="button" class="mode-text-btn${m.id === state.mode ? ' active' : ''}" data-mode="${m.id}">${m.label.toLowerCase()}</button>`
      return i < MODES.length - 1 ? btn + '<span class="mode-sep">/</span>' : btn
    }).join('')
  }

  function setMode(modeId) {
    const mode = MODES.find(m => m.id === modeId)
    if (!mode) return
    state.mode = modeId
    renderModeToggle()
    const input = document.getElementById('ai-chat-input')
    if (input) input.placeholder = mode.placeholder
  }

  /* ── Model / agent select dropdowns ──────────── */

  function renderSelectMenu(type) {
    const items = type === 'model' ? MODELS : AGENTS
    const selectedId = type === 'model' ? state.modelId : state.agentId
    const menu = document.getElementById(`ai-chat-${type}-menu`)
    const label = document.getElementById(`ai-chat-${type}-label`)
    if (!menu) return

    let html = ''
    let lastGroup = null
    items.forEach(item => {
      if (type === 'model' && item.group !== lastGroup) {
        html += `<div class="select-group-label">${item.group}</div>`
        lastGroup = item.group
      }
      const selected = item.id === selectedId
      html += `
        <div class="select-item${selected ? ' selected' : ''}" data-type="${type}" data-id="${item.id}">
          <div class="select-item-text">
            <span class="select-item-name">${item.name}</span>
            <span class="select-item-desc">${item.desc}</span>
          </div>
          ${selected ? ICON_CHECK : ''}
        </div>`
    })
    menu.innerHTML = html

    if (label) {
      const current = items.find(i => i.id === selectedId)
      label.textContent = current ? current.name : ''
    }
  }

  function toggleSelect(type) {
    const el = document.getElementById(`ai-chat-${type}-select`)
    if (!el) return
    const willOpen = !el.classList.contains('open')
    closeAllSelects()
    if (willOpen) el.classList.add('open')
  }

  function closeAllSelects() {
    document.querySelectorAll('.ai-chat-select.open').forEach(el => el.classList.remove('open'))
    document.querySelectorAll('.context-add-wrap.open').forEach(el => el.classList.remove('open'))
  }

  function chooseSelectItem(type, id) {
    if (type === 'model') state.modelId = id
    if (type === 'agent') state.agentId = id
    renderSelectMenu(type)
    closeAllSelects()
    announceSwitch(type, id)
  }

  function announceSwitch(type, id) {
    const items = type === 'model' ? MODELS : AGENTS
    const item = items.find(i => i.id === id)
    if (!item) return
    const history = document.getElementById('ai-chat-history')
    if (!history) return
    const note = document.createElement('div')
    note.className = 'ai-chat-msg system'
    note.textContent = type === 'model'
      ? `Switched model to ${item.name}`
      : `Now talking to ${item.name}`
    history.appendChild(note)
    history.scrollTop = history.scrollHeight
  }

  /* ── Context chips ("+ Add context") ─────────── */

  function renderContextRow() {
    const row = document.getElementById('ai-chat-context-row')
    if (!row) return
    const chipsHtml = state.contextChips.map(id => {
      const item = CONTEXT_ITEMS.find(c => c.id === id)
      if (!item) return ''
      return `
        <span class="context-chip context-chip-added" data-chip="${id}">
          ${esc(item.label)}
          <button class="chip-remove" data-remove-chip="${id}" title="Remove">${ICON_X_SMALL}</button>
        </span>`
    }).join('')

    row.innerHTML = `
      <div class="context-add-wrap" id="ai-chat-context-wrap">
        <button class="context-chip context-chip-add" data-action="toggle-context">+ Add context</button>
        <div class="ai-chat-select-menu" id="ai-chat-context-menu"></div>
      </div>
      ${chipsHtml}`
    renderContextMenu()
  }

  function renderContextMenu() {
    const menu = document.getElementById('ai-chat-context-menu')
    if (!menu) return
    const available = CONTEXT_ITEMS.filter(c => !state.contextChips.includes(c.id))
    menu.innerHTML = available.length
      ? available.map(c => `
          <div class="select-item" data-context-item="${c.id}">
            <div class="select-item-text"><span class="select-item-name">${esc(c.label)}</span></div>
          </div>`).join('')
      : `<div class="select-item" style="cursor:default;color:var(--text-muted,#5e5b54)">All context added</div>`
  }

  function toggleContextMenu() {
    const wrap = document.getElementById('ai-chat-context-wrap')
    if (!wrap) return
    const willOpen = !wrap.classList.contains('open')
    closeAllSelects()
    if (willOpen) wrap.classList.add('open')
  }

  function addContextItem(id) {
    if (!state.contextChips.includes(id)) state.contextChips.push(id)
    renderContextRow()
    closeAllSelects()
  }

  function removeContextChip(id) {
    state.contextChips = state.contextChips.filter(c => c !== id)
    renderContextRow()
  }

  /* ── Session history overlay ─────────────────── */

  function toggleAiChatHistoryPanel(forceState) {
    const panel = document.getElementById('ai-chat-history-panel')
    const header = document.querySelector('.ai-chat-header')
    if (!panel) return
    const open = typeof forceState === 'boolean' ? forceState : !panel.classList.contains('open')
    state.historyOpen = open
    if (open) {
      const top = header ? header.offsetHeight : 0
      panel.style.top = top + 'px'
      renderHistoryList()
      panel.classList.add('open')
      const search = document.getElementById('ai-chat-history-search')
      if (search) setTimeout(() => search.focus(), 80)
    } else {
      panel.classList.remove('open')
    }
  }

  function relativeGroup(ts) {
    const now = Date.now()
    const diffH = (now - ts) / 3600000
    if (diffH < 20) return 'Today'
    if (diffH < 44) return 'Yesterday'
    if (diffH < 24 * 7) return 'Previous 7 days'
    return 'Older'
  }

  function formatTime(ts) {
    const diffH = (Date.now() - ts) / 3600000
    if (diffH < 1) return Math.max(1, Math.round(diffH * 60)) + 'm'
    if (diffH < 24) return Math.round(diffH) + 'h'
    return Math.round(diffH / 24) + 'd'
  }

  function renderHistoryList(filter) {
    const list = document.getElementById('ai-chat-history-list')
    if (!list) return
    const q = (filter || '').trim().toLowerCase()
    const sessions = state.sessions
      .filter(s => !q || s.title.toLowerCase().includes(q))
      .sort((a, b) => b.updated - a.updated)

    if (!sessions.length) {
      list.innerHTML = `<div class="history-empty">${q ? 'No chats match your search.' : 'No previous chats yet.'}</div>`
      return
    }

    let html = ''
    let lastGroup = null
    sessions.forEach(s => {
      const group = relativeGroup(s.updated)
      if (group !== lastGroup) {
        html += `<div class="history-group-label">${group}</div>`
        lastGroup = group
      }
      const preview = s.messages.length ? s.messages[s.messages.length - 1].text : ''
      const active = s.id === state.currentSessionId
      html += `
        <div class="history-item${active ? ' active' : ''}" data-session-id="${s.id}">
          <div class="history-item-body">
            <div class="history-item-title">${esc(s.title)}</div>
            <div class="history-item-preview">${esc(preview)}</div>
          </div>
          <div class="history-item-time">${formatTime(s.updated)}</div>
          <button class="history-item-del" data-del-session="${s.id}" title="Delete chat">${ICON_TRASH}</button>
        </div>`
    })
    list.innerHTML = html
  }

  function loadSession(id) {
    const session = state.sessions.find(s => s.id === id)
    if (!session) return
    state.currentSessionId = id
    const history = document.getElementById('ai-chat-history')
    if (history) {
      history.innerHTML = session.messages.map(m => renderMsgHTML(m.role, m.text)).join('')
      history.scrollTop = history.scrollHeight
    }
    toggleAiChatHistoryPanel(false)
  }

  function deleteSession(id) {
    state.sessions = state.sessions.filter(s => s.id !== id)
    if (state.currentSessionId === id) {
      state.currentSessionId = null
    }
    renderHistoryList(document.getElementById('ai-chat-history-search')?.value)
  }

  function renderMsgHTML(role, text) {
    if (role === 'user') {
      return `<div class="ai-chat-msg user"><div class="ai-chat-text">${esc(text)}</div></div>`
    }
    return `<div class="ai-chat-msg agent"><div class="ai-chat-role">Horo</div><div class="ai-chat-text">${esc(text)}</div></div>`
  }

  /* ── New chat ─────────────────────────────────── */

  function newAiChatSession() {
    archiveCurrentSession()
    state.currentSessionId = null
    state.contextChips = []
    renderContextRow()
    const history = document.getElementById('ai-chat-history')
    if (history) {
      history.innerHTML = `
        <div class="ai-chat-msg agent">
          <div class="ai-chat-role">Horo</div>
          <div class="ai-chat-text">Ready. Ask me about the engine, scene editing, or any workflow.</div>
        </div>`
    }
    toggleAiChatHistoryPanel(false)
    const input = document.getElementById('ai-chat-input')
    if (input) setTimeout(() => input.focus(), 80)
  }

  function archiveCurrentSession() {
    const history = document.getElementById('ai-chat-history')
    if (!history) return
    const userMsgs = history.querySelectorAll('.ai-chat-msg.user .ai-chat-text')
    if (!userMsgs.length) return // nothing worth saving

    if (state.currentSessionId) {
      // already a saved session, just bump its timestamp
      const existing = state.sessions.find(s => s.id === state.currentSessionId)
      if (existing) existing.updated = Date.now()
      return
    }

    const messages = []
    history.querySelectorAll('.ai-chat-msg').forEach(node => {
      if (node.classList.contains('system')) return
      const role = node.classList.contains('user') ? 'user' : 'agent'
      const text = node.querySelector('.ai-chat-text')?.textContent || ''
      messages.push({ role, text })
    })
    const firstUser = messages.find(m => m.role === 'user')
    state.sessions.unshift({
      id: 's' + Date.now(),
      title: (firstUser ? firstUser.text : 'New chat').slice(0, 48),
      updated: Date.now(),
      messages,
    })
  }

  /* ── Respect status bar ──────────────────────── */

  function syncPanelBottom() {
    const panel = document.getElementById('ai-chat-panel')
    if (!panel) return
    // Find any fixed bottom bar: .status-bar, footer, [role="contentinfo"]
    const bar = document.querySelector('.status-bar, footer, [role="contentinfo"]')
    let bottom = 0
    if (bar) {
      const rect = bar.getBoundingClientRect()
      // Only count it if it's actually at the bottom of the viewport
      if (rect.bottom >= window.innerHeight - 2) {
        bottom = Math.max(bottom, rect.height)
      }
    }
    panel.style.bottom = bottom + 'px'
    if (state.historyOpen) toggleAiChatHistoryPanel(true)
  }

  /* ── Activity-bar button injection ───────────── */

  const AI_CHAT_ICON =
    '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round">' +
    '<path d="M3 2h10a1 1 0 011 1v8a1 1 0 01-1 1H7l-3 3V3a1 1 0 011-1z"/>' +
    '<circle cx="6" cy="7" r=".7" fill="currentColor" stroke="none"/>' +
    '<circle cx="8.5" cy="7" r=".7" fill="currentColor" stroke="none"/>' +
    '<circle cx="11" cy="7" r=".7" fill="currentColor" stroke="none"/>' +
    '</svg>'

  function injectButtons() {
    const bars = document.querySelectorAll('.activity-bar-right')
    bars.forEach(bar => {
      if (bar.querySelector('[data-ai-chat]')) return
      const btn = document.createElement('button')
      btn.className = 'activity-btn'
      btn.type = 'button'
      btn.title = 'AI Chat'
      btn.setAttribute('data-ai-chat', '')
      btn.onclick = toggleAiChat
      btn.innerHTML = AI_CHAT_ICON

      const spacer = bar.querySelector('.activity-spacer')
      if (spacer) {
        bar.insertBefore(btn, spacer)
      } else {
        bar.appendChild(btn)
      }
    })
  }

  /* ── Delegated events inside the panel ────────── */

  function bindPanelEvents(panel) {
    panel.addEventListener('click', e => {
      const modeBtn = e.target.closest('[data-mode]')
      if (modeBtn) { setMode(modeBtn.dataset.mode); return }

      const selectTrigger = e.target.closest('[data-action="toggle-select"]')
      if (selectTrigger) { toggleSelect(selectTrigger.dataset.type); return }

      const contextToggle = e.target.closest('[data-action="toggle-context"]')
      if (contextToggle) { toggleContextMenu(); return }

      const contextItem = e.target.closest('[data-context-item]')
      if (contextItem) { addContextItem(contextItem.dataset.contextItem); return }

      const removeChip = e.target.closest('[data-remove-chip]')
      if (removeChip) { e.stopPropagation(); removeContextChip(removeChip.dataset.removeChip); return }

      const selectItem = e.target.closest('.select-item[data-type]')
      if (selectItem) { chooseSelectItem(selectItem.dataset.type, selectItem.dataset.id); return }

      const histToggle = e.target.closest('[data-action="toggle-history"]')
      if (histToggle) { toggleAiChatHistoryPanel(); return }

      const newChatBtn = e.target.closest('[data-action="new-chat"]')
      if (newChatBtn) { newAiChatSession(); return }

      const delBtn = e.target.closest('[data-del-session]')
      if (delBtn) { e.stopPropagation(); deleteSession(delBtn.dataset.delSession); return }

      const histItem = e.target.closest('.history-item')
      if (histItem) { loadSession(histItem.dataset.sessionId); return }
    })

    const search = panel.querySelector('#ai-chat-history-search')
    if (search) {
      search.addEventListener('input', () => renderHistoryList(search.value))
    }
  }

  // Close open dropdowns when clicking anywhere outside them
  document.addEventListener('click', e => {
    if (!e.target.closest('.ai-chat-select') && !e.target.closest('.context-add-wrap')) closeAllSelects()
  })

  /* ── Public API ─────────────────────────────── */

  window.toggleAiChat = function () {
    const panel = ensurePanel()
    const isOpen = panel.classList.toggle('open')
    if (isOpen) {
      const input = document.getElementById('ai-chat-input')
      if (input) setTimeout(() => input.focus(), 120)
    } else {
      toggleAiChatHistoryPanel(false)
      closeAllSelects()
    }
    document.querySelectorAll('[data-ai-chat]').forEach(btn => {
      btn.classList.toggle('active', isOpen)
    })
  }

  window.toggleAiChatHistory = function () {
    toggleAiChatHistoryPanel()
  }

  window.newAiChatSession = function () {
    newAiChatSession()
  }

  window.sendAiChatMessage = function () {
    const input = document.getElementById('ai-chat-input')
    const history = document.getElementById('ai-chat-history')
    if (!input || !history) return
    const text = input.value.trim()
    if (!text) return

    // User message
    const userMsg = document.createElement('div')
    userMsg.className = 'ai-chat-msg user'
    userMsg.innerHTML =
      '<div class="ai-chat-text">' + esc(text) + '</div>'
    history.appendChild(userMsg)

    // Agent response (static demo) — reflects current mode/agent/model
    const agent = AGENTS.find(a => a.id === state.agentId)
    const model = MODELS.find(m => m.id === state.modelId)
    const mode = MODES.find(m => m.id === state.mode)

    const agentMsg = document.createElement('div')
    agentMsg.className = 'ai-chat-msg agent'
    agentMsg.innerHTML =
      '<div class="ai-chat-role">' + esc(agent ? agent.name : 'Horo') + '</div>' +
      '<div class="ai-chat-text">[' + esc(mode ? mode.label : 'Ask') + ' · ' + esc(model ? model.name : '') + '] Received: <code>' +
      esc(text.slice(0, 60)) + '</code> — static demo; real agent will connect via the Horo MCP bridge.</div>'
    history.appendChild(agentMsg)

    input.value = ''
    input.style.height = 'auto'
    history.scrollTop = history.scrollHeight

    archiveCurrentSession()
    if (!state.currentSessionId) {
      // archiveCurrentSession() just created a new entry at the front; adopt it
      const created = state.sessions[0]
      if (created) state.currentSessionId = created.id
    }
  }

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  }

  /* ── Auto-resize textarea ───────────────────── */

  function autoResize() {
    const ta = document.getElementById('ai-chat-input')
    if (!ta) return
    ta.style.height = 'auto'
    ta.style.height = Math.min(ta.scrollHeight, 120) + 'px'
  }
  document.addEventListener('input', e => {
    if (e.target && e.target.id === 'ai-chat-input') autoResize()
  })

  /* ── Init ───────────────────────────────────── */

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
      ensurePanel()
      injectButtons()
      syncPanelBottom()
    })
  } else {
    ensurePanel()
    injectButtons()
    syncPanelBottom()
  }

  window.addEventListener('resize', syncPanelBottom)

  /* ── Escape key ─────────────────────────────── */

  document.addEventListener('keydown', e => {
    if (e.key !== 'Escape') return
    const panel = document.getElementById('ai-chat-panel')
    if (!panel || !panel.classList.contains('open')) return
    // Don't steal Escape if a modal is visible
    const modal = document.getElementById('modal-overlay')
    if (modal && modal.classList.contains('active')) return

    if (state.historyOpen) {
      e.preventDefault()
      toggleAiChatHistoryPanel(false)
      return
    }
    if (document.querySelector('.ai-chat-select.open')) {
      e.preventDefault()
      closeAllSelects()
      return
    }
    e.preventDefault()
    panel.classList.remove('open')
    document.querySelectorAll('[data-ai-chat]').forEach(btn => btn.classList.remove('active'))
  })
})()
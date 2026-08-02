(() => {
  'use strict';

  // The HTML architecture screens use the same editor catalogs as HoroEditor.
  // Keep this adapter data-driven: the page owns stable semantic keys, while
  // translations remain in assets/localization/editor/*.json.
  const sourceKeys = {
    'Horo Editor — Workspace': 'editor_workspace.title',
    'File': 'web_workspace.menu.file',
    'Edit': 'web_workspace.menu.edit',
    'Assets': 'web_workspace.menu.assets',
    'GameObject': 'web_workspace.menu.game_object',
    'Component': 'web_workspace.menu.component',
    'Window': 'web_workspace.menu.window',
    'Build': 'web_workspace.menu.build',
    'Help': 'web_workspace.menu.help',
    'Workspace': 'web_workspace.menu.workspace',
    'Asset Editors': 'web_workspace.menu.asset_editors',
    'Tools': 'web_workspace.menu.tools',
    'Diagnostics': 'web_workspace.menu.diagnostics',
    'Runtime': 'web_workspace.menu.runtime',
    'Localization Editor': 'web_workspace.menu.localization_editor',
    'New Project': 'web_workspace.menu.new_project',
    'Open Project': 'web_workspace.menu.open_project',
    'Open Recent': 'web_workspace.menu.open_recent',
    'Save': 'web_workspace.menu.save',
    'Save As…': 'web_workspace.menu.save_as',
    'Exit': 'web_workspace.menu.exit',
    'Undo': 'web_workspace.menu.undo',
    'Redo': 'web_workspace.menu.redo',
    'Cut': 'web_workspace.menu.cut',
    'Copy': 'web_workspace.menu.copy',
    'Paste': 'web_workspace.menu.paste',
    'Preferences…': 'web_workspace.menu.preferences',
    'Import New Asset…': 'web_workspace.menu.import_asset',
    'Reimport All': 'web_workspace.menu.reimport_all',
    'Refresh Asset Index': 'web_workspace.menu.refresh_asset_index',
    'Create Empty': 'web_workspace.menu.create_empty',
    'Create Primitive →': 'web_workspace.menu.create_primitive',
    'Create Light →': 'web_workspace.menu.create_light',
    'Add Component…': 'web_workspace.menu.add_component',
    'Documentation': 'web_workspace.menu.documentation',
    'Editor Settings': 'settings.title',
    'opens modal': 'web_workspace.accessibility.opens_modal',
    'opens screen': 'web_workspace.accessibility.opens_screen',
    'Find in Scene': 'web_workspace.activity.find_in_scene',
    'Scene Settings': 'web_workspace.activity.scene_settings',
    'Project Settings': 'web_workspace.activity.project_settings',
    'Build Output': 'web_workspace.activity.build_output',
    'Render Settings': 'web_workspace.activity.render_settings',
    'AI Chat': 'web_workspace.activity.ai_chat',
    'Chat history': 'web_workspace.ai_chat.history',
    'New chat': 'web_workspace.ai_chat.new_chat',
    'Close chat': 'web_workspace.ai_chat.close_chat',
    'Ready. Ask me about the engine, scene editing, or any workflow.': 'web_workspace.ai_chat.ready',
    '+ Add context': 'web_workspace.ai_chat.add_context',
    'Ask Horo…': 'web_workspace.ai_chat.placeholder',
    'Send': 'web_workspace.ai_chat.send',
    'Previous chats': 'web_workspace.ai_chat.previous_chats',
    'Language': 'web_workspace.language',
    'HORO AI CHAT': 'web_workspace.ai_chat.title',
    'Horo AI Chat': 'web_workspace.ai_chat.title',
    'ask': 'web_workspace.ai_chat.mode.ask',
    'agent': 'web_workspace.ai_chat.mode.agent',
    'edit': 'web_workspace.ai_chat.mode.edit',
    'On': 'web_workspace.value.on',
    '✓ Static': 'web_workspace.value.static',
    'Hierarchy': 'web_workspace.panel.hierarchy',
    'Inspector': 'web_workspace.panel.inspector',
    'Scene': 'web_workspace.panel.scene',
    'Content Browser': 'web_workspace.panel.content_browser',
    'Transform': 'web_workspace.inspector.transform',
    'Position': 'web_workspace.inspector.position',
    'Rotation': 'web_workspace.inspector.rotation',
    'Scale': 'web_workspace.inspector.scale',
    'Mesh Renderer': 'web_workspace.inspector.mesh_renderer',
    'Mesh': 'web_workspace.inspector.mesh',
    'Material': 'web_workspace.inspector.material',
    'Shadows': 'web_workspace.inspector.shadows',
    'Static Flags': 'web_workspace.inspector.static_flags',
    'Batching': 'web_workspace.inspector.batching',
    'Lightmap': 'web_workspace.inspector.lightmap',
    'Static': 'web_workspace.inspector.static',
    'Select': 'web_workspace.toolbar.select',
    'Move': 'web_workspace.toolbar.move',
    'Rotate': 'web_workspace.toolbar.rotate',
    'Scale': 'web_workspace.toolbar.scale',
    'Play': 'web_workspace.toolbar.play',
    'Game': 'web_workspace.toolbar.game',
    'Local space': 'web_workspace.toolbar.local_space',
    'World space': 'web_workspace.toolbar.world_space',
    'Perspective · Shaded': 'web_workspace.viewport.perspective_shaded',
    'Shaded': 'settings.rendering.shaded',
    'Wire': 'web_workspace.viewport.wire',
    'Lit': 'settings.rendering.lit',
    'Assets': 'web_workspace.dock.assets',
    'Console': 'web_workspace.dock.console',
    'MCP': 'web_workspace.dock.mcp',
    'Perf': 'web_workspace.dock.performance',
    'Physics': 'web_workspace.dock.physics',
    'Audio': 'web_workspace.dock.audio',
    'Net': 'web_workspace.dock.network',
    'L10n': 'web_workspace.dock.localization',
    'EMBEDDED': 'web_workspace.label.embedded',
    'BRIDGE': 'web_workspace.label.bridge',
    'TOOLS': 'web_workspace.label.tools',
    'GPU': 'web_workspace.label.gpu',
    'CPU': 'web_workspace.label.cpu',
    'MEM': 'web_workspace.label.memory',
    'SOLVER': 'web_workspace.label.solver',
    'LAYERS': 'web_workspace.label.layers',
    'MASTER': 'web_workspace.label.master',
    'BUSSES': 'web_workspace.label.busses',
    'DEVICE': 'web_workspace.label.device',
    'PING': 'web_workspace.label.ping',
    'REP': 'web_workspace.label.replication',
    'CONN': 'web_workspace.label.connection',
    'LOCALE': 'web_workspace.label.locale',
    'STRINGS': 'web_workspace.label.strings',
    'FONTS': 'web_workspace.label.fonts',
    'Project asset dock': 'web_workspace.dock.project_assets',
    'Console preview': 'web_workspace.dock.console_preview',
    'MCP preview': 'web_workspace.dock.mcp_preview',
    'Performance preview': 'web_workspace.dock.performance_preview',
    'Physics debug preview': 'web_workspace.dock.physics_preview',
    'Audio mixer preview': 'web_workspace.dock.audio_preview',
    'Network debug preview': 'web_workspace.dock.network_preview',
    'Localization preview': 'web_workspace.dock.localization_preview',
    'Open Asset Browser →': 'web_workspace.action.open_asset_browser',
    'Open Console Panel →': 'web_workspace.action.open_console',
    'Open MCP Panel →': 'web_workspace.action.open_mcp',
    'Open Observability Dashboard →': 'web_workspace.action.open_observability',
    'Open Physics Debugger →': 'web_workspace.action.open_physics',
    'Open Audio Mixer →': 'web_workspace.action.open_audio',
    'Open Network Debugger →': 'web_workspace.action.open_network',
    'Open Localization Editor →': 'web_workspace.action.open_localization',
    'Unsaved': 'web_workspace.status.unsaved',
    'Sel': 'web_workspace.status.selection',
    'Rev': 'web_workspace.status.revision',
    'Nav': 'web_workspace.status.navigation',
    'idle': 'web_workspace.status.idle',
    'Search objects…': 'web_workspace.search_objects',
    'Panel': 'web_workspace.modal.panel',
    'Close': 'web_workspace.action.close',
    'Toggle bottom dock': 'web_workspace.action.toggle_bottom_dock',
    'Camera': 'web_workspace.type.camera',
    'Light': 'web_workspace.type.light',
    'Mesh': 'web_workspace.type.mesh'
  };

  const catalogPath = (locale) => {
    const file = `${locale}.json`;
    return new URL(`../../../assets/localization/editor/${file}`, document.baseURI).href;
  };

  let messages = {};
  let sourceToText = {};

  function localized(source) {
    const key = sourceKeys[source] || source;
    return messages[key] || source;
  }

  function applyText(root) {
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    const nodes = [];
    let node;
    while ((node = walker.nextNode())) nodes.push(node);

    for (const textNode of nodes) {
      const source = textNode.nodeValue.trim();
      if (!source) continue;
      const isInspectorScale = source === 'Scale' && textNode.parentElement?.matches('.prop-row label');
      const translated = isInspectorScale ? (messages['web_workspace.inspector.scale'] || source) : localized(source);
      if (translated !== source) {
        textNode.nodeValue = textNode.nodeValue.replace(source, translated);
        continue;
      }
      // Shared menu entries append a non-localized accessibility suffix.
      for (const candidate of Object.keys(sourceKeys)) {
        if (source.startsWith(`${candidate} opens `)) {
          textNode.nodeValue = textNode.nodeValue.replace(candidate, localized(candidate));
          textNode.nodeValue = textNode.nodeValue.replace(' opens modal', ` ${localized('opens modal')}`);
          textNode.nodeValue = textNode.nodeValue.replace(' opens screen', ` ${localized('opens screen')}`);
          break;
        }
      }
    }

    root.querySelectorAll('[title],[aria-label],[placeholder],[data-modal-title],[value]').forEach((element) => {
      for (const attribute of ['title', 'aria-label', 'placeholder', 'data-modal-title', 'value']) {
        if (element.hasAttribute(attribute)) {
          const value = element.getAttribute(attribute);
          const translated = localized(value);
          if (translated !== value) element.setAttribute(attribute, translated);
        }
      }
    });
  }

  function addLocaleSelector() {
    const statusRight = document.querySelector('.status-right');
    if (!statusRight || document.getElementById('horo-web-locale')) return;
    const select = document.createElement('select');
    select.id = 'horo-web-locale';
    select.title = localized('Language');
    select.setAttribute('aria-label', localized('Language'));
    select.style.cssText = 'height:20px;padding:0 4px;background:var(--bg-input);border:1px solid var(--border);border-radius:3px;color:var(--text-muted);font:10px var(--font-mono);';
    select.innerHTML = '<option value="en-US">EN</option><option value="tr-TR">TR</option>';
    select.value = new URLSearchParams(window.location.search).get('lang') || localStorage.getItem('horo-web-locale') || 'en-US';
    select.addEventListener('change', () => {
      localStorage.setItem('horo-web-locale', select.value);
      window.location.reload();
    });
    statusRight.prepend(select);
  }

  async function activate() {
    const requested = new URLSearchParams(window.location.search).get('lang') || localStorage.getItem('horo-web-locale') || 'en-US';
    const locale = requested === 'tr-TR' ? 'tr-TR' : 'en-US';
    try {
      const response = await fetch(catalogPath(locale));
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const catalog = await response.json();
      messages = Object.fromEntries(Object.entries(catalog.messages || {}).map(([key, value]) => [key, value.text]));
      sourceToText = Object.fromEntries(Object.keys(sourceKeys).map((source) => [source, localized(source)]));
      document.documentElement.lang = locale;
      document.title = localized('Horo Editor — Workspace');
      applyText(document.body);
      const observer = new MutationObserver((mutations) => {
        for (const mutation of mutations) {
          for (const addedNode of mutation.addedNodes) {
            if (addedNode.nodeType === Node.ELEMENT_NODE) applyText(addedNode);
          }
        }
      });
      observer.observe(document.body, { childList: true, subtree: true });
      addLocaleSelector();
      if (typeof window.openModal === 'function' && !window.openModal.__horoLocalized) {
        const openModal = window.openModal;
        const localizedOpenModal = (title, source) => openModal(localized(title), source);
        localizedOpenModal.__horoLocalized = true;
        window.openModal = localizedOpenModal;
      }
      window.dispatchEvent(new CustomEvent('horo-web-locale-ready', { detail: { locale } }));
    } catch (error) {
      console.warn('[horo-web-localization] Catalog unavailable:', error);
    }
  }

  activate();
})();

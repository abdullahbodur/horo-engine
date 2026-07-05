(() => {
  'use strict';

  const architectureRoot = new URL('../', document.currentScript.src);
  const repositoryRoot = new URL('../../../', document.currentScript.src);
  const menuRoot = document.querySelector('[data-horo-menu-bar]');
  if (!menuRoot) return;

  if (window !== window.parent || document.documentElement.classList.contains('embedded')) {
    menuRoot.remove();
    return;
  }

  const url = (path) => new URL(path, architectureRoot).href;
  const assetUrl = (path) => new URL(path, repositoryRoot).href;

  function modal(title, path, label) {
    return `<div class="menu-leaf" role="button" tabindex="0" data-modal-title="${title}" data-modal-path="${path}">${label} opens modal</div>`;
  }

  function screen(path, label) {
    return `<a class="menu-leaf" href="../${path}">${label} opens screen</a>`;
  }

  function submenu(label, content) {
    return `<div class="submenu"><div class="submenu-trigger" role="button" tabindex="0"><span>${label}</span><span class="submenu-arrow">›</span></div><div class="submenu-menu">${content}</div></div>`;
  }

  const windowMenu = `
    ${submenu('Workspace', `
      ${screen('editor/editor-workspace.html', 'Editor Workspace')}
      ${screen('runtime/ui-canvas-editor.html', 'UI Canvas Editor')}
      ${screen('runtime/cinematic-sequencer.html', 'Cinematic Sequencer')}
    `)}
    ${submenu('Asset Editors', `
      ${screen('runtime/animation-editor.html', 'Animation Editor')}
      ${screen('runtime/material-editor.html', 'Material Editor')}
      ${screen('runtime/particle-editor.html', 'Particle Editor')}
      ${screen('runtime/prefab-editor.html', 'Prefab Editor')}
      ${screen('runtime/shader-graph-editor.html', 'Shader Graph')}
    `)}
    ${submenu('Tools', `
      ${modal('Asset Browser', 'editor/asset-browser.html', 'Asset Browser')}
      ${modal('Plugin Manager', 'extensions/plugin-manager.html', 'Plugin Manager')}
      ${modal('Package Manager', 'packages/package-manager.html', 'Package Manager')}
      ${modal('Primitives Panel', 'runtime/primitives-panel.html', 'Primitives Panel')}
      ${modal('Decal Placement', 'runtime/decal-placement.html', 'Decal Placement')}
      ${modal('Destruction Setup', 'runtime/destruction-setup.html', 'Destruction Setup')}
      ${modal('Save/Load Manager', 'runtime/save-load-manager.html', 'Save/Load Manager')}
      ${modal('Post Processing Stack', 'runtime/post-processing-stack.html', 'Post Processing Stack')}
      ${modal('XR Setup', 'runtime/xr-setup.html', 'XR Setup')}
    `)}
    ${submenu('Diagnostics', `
      ${modal('Physics Debugger', 'runtime/physics-debugger.html', 'Physics Debugger')}
      ${modal('Network Debugger', 'runtime/network-debugger.html', 'Network Debugger')}
      ${modal('Navigation Bake', 'runtime/navigation-bake.html', 'Navigation Bake')}
      ${modal('LOD Debugger', 'runtime/lod-debugger.html', 'LOD Debugger')}
      ${modal('Virtual Texturing Debug', 'runtime/virtual-texturing-debug.html', 'Virtual Texturing Debug')}
      ${screen('observability/observability-dashboard.html', 'Observability Dashboard')}
    `)}
    ${submenu('Runtime', `
      ${screen('interfaces/mcp-panel.html', 'MCP Panel')}
      ${screen('runtime/console-panel.html', 'Console Panel')}
      ${screen('runtime/audio-mixer.html', 'Audio Mixer')}
      ${screen('runtime/input-mapping-editor.html', 'Input Mapping Editor')}
      ${screen('runtime/pcg-graph-editor.html', 'PCG Graph Editor')}
      ${screen('extensions/gameplay-behavior-editor.html', 'Gameplay Behavior Editor')}
    `)}
    <div class="sep"></div>
    ${screen('editor/localization-editor.html', 'Localization Editor')}
  `;

  const style = document.createElement('style');
  style.textContent = `
    [data-horo-menu-bar] .submenu {
      position: relative;
      display: block !important;
      min-width: 236px;
      padding: 0 !important;
      margin: 0;
      background: transparent !important;
      font: inherit;
    }
    [data-horo-menu-bar] .submenu-trigger {
      display: flex !important;
      align-items: center;
      justify-content: space-between;
      gap: 18px;
      padding: 6px 14px;
      color: var(--horo-txt, var(--txt, #e8e4d9));
      white-space: nowrap;
      font: 12px var(--horo-mono, var(--mono, monospace));
      cursor: default;
    }
    [data-horo-menu-bar] .submenu:hover > .submenu-trigger,
    [data-horo-menu-bar] .submenu.active > .submenu-trigger { background: var(--horo-hover, var(--hover, #252d39)); }
    [data-horo-menu-bar] .submenu-arrow { color: var(--horo-dim, var(--dim, #6f6a61)); }
    [data-horo-menu-bar] .dropdown .submenu > .submenu-menu {
      display: none !important;
      position: absolute;
      top: 0;
      left: calc(100% - 2px);
      z-index: 120;
      min-width: 244px;
      padding: 5px 0 !important;
      border: 1px solid var(--horo-bd2, var(--bd2, #3a4656));
      border-radius: var(--horo-radius, var(--r, 4px));
      background: var(--horo-bg2, var(--bg2, #181c21)) !important;
      box-shadow: 0 14px 32px rgba(0,0,0,.48);
    }
    [data-horo-menu-bar] .dropdown .submenu > .submenu-menu:hover {
      background: var(--horo-bg2, var(--bg2, #181c21)) !important;
    }
    [data-horo-menu-bar] .dropdown .submenu.active > .submenu-menu { display: block !important; }
    [data-horo-menu-bar] .submenu-menu > .menu-leaf {
      display: block !important;
      padding: 6px 14px;
      background: transparent !important;
      color: var(--horo-txt, var(--txt, #e8e4d9));
      text-decoration: none;
      white-space: nowrap;
      font: 12px var(--horo-mono, var(--mono, monospace));
    }
    [data-horo-menu-bar] .submenu-menu > .menu-leaf:hover,
    [data-horo-menu-bar] .submenu-menu > .menu-leaf:focus {
      background: var(--horo-hover, var(--hover, #252d39)) !important;
      color: var(--horo-txt, var(--txt, #e8e4d9));
      outline: none;
    }
  `;
  document.head.appendChild(style);

  menuRoot.innerHTML = `
    <a class="app-logo" href="${url('editor/welcome-screen.html')}"><img src="${assetUrl('assets/launcher/logo.png')}" alt="HORO" height="22"></a>
    <div class="menu-item" role="button" tabindex="0">File
      <div class="dropdown">
        ${modal('New Project', 'editor/new-project-wizard.html', 'New Project')}
        ${modal('Open Project', 'editor/welcome-screen.html', 'Open Project')}
        ${modal('Open Recent', 'editor/welcome-screen.html', 'Open Recent')}
        <div class="sep"></div>
        <div class="disabled">Save</div>
        <div class="disabled">Save As…</div>
        <div class="sep"></div>
        ${modal('Editor Settings', 'editor/settings-modal.html', 'Editor Settings')}
        <div class="disabled">Exit</div>
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Edit
      <div class="dropdown">
        <div class="disabled">Undo</div>
        <div class="disabled">Redo</div>
        <div class="sep"></div>
        <div class="disabled">Cut</div>
        <div class="disabled">Copy</div>
        <div class="disabled">Paste</div>
        <div class="sep"></div>
        ${modal('Preferences', 'editor/settings-modal.html', 'Preferences…')}
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Assets
      <div class="dropdown">
        ${modal('Import New Asset…', 'runtime/asset-import-modal.html', 'Import New Asset…')}
        <div class="disabled">Reimport All</div>
        <div class="disabled">Refresh Asset Index</div>
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">GameObject
      <div class="dropdown">
        <div class="disabled">Create Empty</div>
        <div class="disabled">Create Primitive →</div>
        <div class="disabled">Create Light →</div>
        <div class="sep"></div>
        ${modal('Character Setup', 'runtime/character-setup.html', 'Character Setup…')}
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Component
      <div class="dropdown"><div class="disabled">Add Component…</div></div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Window
      <div class="dropdown">
        ${windowMenu}
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Build
      <div class="dropdown">
        ${modal('Build & Release…', 'release/release-modal-design.html', 'Build & Release…')}
        <div class="sep"></div>
        ${modal('Build Output', 'runtime/build-output.html', 'Build Output')}
        ${modal('Project Settings', 'editor/project-settings.html', 'Project Settings')}
        ${modal('Render Settings', 'runtime/render-settings.html', 'Render Settings')}
        ${modal('Platform Services', 'runtime/platform-services-config.html', 'Platform Services')}
        ${modal('Gameplay Integration', 'extensions/module-config.html', 'Gameplay Integration')}
        <div class="sep"></div>
        <div class="disabled">Clean Build</div>
        <div class="disabled">Validate Project</div>
      </div>
    </div>
    <div class="menu-item" role="button" tabindex="0">Help
      <div class="dropdown"><div class="disabled">Documentation</div></div>
    </div>
    <div class="spacer"></div>
    <div class="project-tag">Horo Engine 0.9.0-dev</div>
  `;

  function openModalFallback(title, path) {
    const target = url(path);
    if (typeof window.openModal === 'function' && window.openModal !== openModalFallback) {
      window.openModal(title, target);
      return;
    }

    const overlay = document.getElementById('modal-overlay');
    const iframe = document.getElementById('modal-iframe');
    const titleNode = document.getElementById('modal-title');
    if (!overlay || !iframe) {
      window.location.href = target;
      return;
    }

    if (titleNode) titleNode.textContent = title;
    iframe.src = target;
    iframe.style.display = 'block';
    overlay.classList.add('active');
    overlay.style.display = 'flex';
  }

  if (typeof window.openModal !== 'function') window.openModal = openModalFallback;

  function closeMenus() {
    menuRoot.querySelectorAll('.menu-item.active').forEach((item) => item.classList.remove('active'));
    menuRoot.querySelectorAll('.submenu.active').forEach((item) => item.classList.remove('active'));
  }

  menuRoot.querySelectorAll('.menu-item').forEach((item) => {
    item.addEventListener('click', (event) => {
      event.stopPropagation();
      const wasActive = item.classList.contains('active');
      closeMenus();
      if (!wasActive) item.classList.add('active');
    });
    item.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        item.click();
      }
    });
  });

  menuRoot.querySelectorAll('[data-modal-path]').forEach((item) => {
    item.addEventListener('click', (event) => {
      event.stopPropagation();
      closeMenus();
      window.openModal(item.dataset.modalTitle, url(item.dataset.modalPath));
    });
    item.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        item.click();
      }
    });
  });

  menuRoot.querySelectorAll('.submenu').forEach((item) => {
    const trigger = item.querySelector('.submenu-trigger');
    item.addEventListener('mouseenter', () => {
      menuRoot.querySelectorAll('.submenu.active').forEach((submenuItem) => submenuItem.classList.remove('active'));
      item.classList.add('active');
    });
    if (!trigger) return;
    trigger.addEventListener('click', (event) => {
      event.stopPropagation();
      menuRoot.querySelectorAll('.submenu.active').forEach((submenuItem) => submenuItem.classList.remove('active'));
      item.classList.add('active');
    });
    trigger.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        trigger.click();
      }
      if (event.key === 'Escape') item.classList.remove('active');
    });
  });

  menuRoot.querySelectorAll('.dropdown a').forEach((anchor) => {
    anchor.addEventListener('click', () => closeMenus());
  });

  document.addEventListener('click', closeMenus);
})();

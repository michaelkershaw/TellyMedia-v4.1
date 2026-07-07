'use strict';

// ════════════════════════════════════════════════════════════════════════════
// TellyMedia v4 — Web UI Bridge
// C++ posts state JSON via webview message; JS sends commands back via postMessage.
// ════════════════════════════════════════════════════════════════════════════

const App = {
  state: null,
  gridScrollY: 0,
  panelListScrollY: 0,
  sidebarScrollY: 0,
  dragState: null,

  // ── Init ──────────────────────────────────────────────────────────────────
  init() {
    this.bindHeader();
    this.bindSidebar();
    this.bindGrid();
    this.bindLayout();
    this.bindOptions();
    this.bindLicense();
    this.bindKeyboard();
    // Request initial state from C++
    this.send({ cmd: 'init' });
  },

  // ── Message bridge ────────────────────────────────────────────────────────
  send(msg) {
    const json = JSON.stringify(msg);
    if (window.chrome && window.chrome.webview) {
      // WebView2 (Windows)
      window.chrome.webview.postMessage(json);
    } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.tmBridge) {
      // WKWebView (macOS)
      window.webkit.messageHandlers.tmBridge.postMessage(json);
    } else {
      // Dev mode — console
      console.log('[tm→native]', json);
    }
  },

  // Called by native code: window.tmOnMessage(json)
  onMessage(json) {
    let msg;
    try { msg = JSON.parse(json); } catch (e) { console.error('parse error', e); return; }

    if (msg.type === 'state') {
      this.state = msg.data;
      this.render();
    } else if (msg.type === 'event') {
      this.handleEvent(msg.name, msg.data);
    } else if (msg.type === 'thumb') {
      this.applyThumbnail(msg.slot, msg.bank, msg.data);
    } else if (msg.type === 'panelImage') {
      this.applyPanelImage(msg.index, msg.data);
    } else if (msg.type === 'nowPlaying') {
      this.updateNowPlaying(msg.data);
    }
  },

  // ── Render ────────────────────────────────────────────────────────────────
  render() {
    if (!this.state) return;
    this.renderTabs();
    this.renderLicenseBadge();
    this.renderBankBar();
    this.renderGrid();
    this.renderSidebar();
    this.renderLayout();
    this.renderOptions();
    this.renderLicense();
    this.renderStatus();
  },

  // ── Header / Tabs ─────────────────────────────────────────────────────────
  bindHeader() {
    document.querySelectorAll('.tab').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = parseInt(btn.dataset.tab);
        this.send({ cmd: 'selectTab', args: { tab } });
      });
    });
    document.getElementById('close-btn').addEventListener('click', () => {
      this.send({ cmd: 'closeWindow' });
    });
  },

  renderTabs() {
    const cur = this.state.curTab;
    document.querySelectorAll('.tab').forEach(btn => {
      const t = parseInt(btn.dataset.tab);
      btn.classList.toggle('active', t === cur);
    });
    const tabs = ['tab-media', 'tab-layout', 'tab-options', 'tab-license'];
    tabs.forEach((id, i) => {
      const el = document.getElementById(id);
      if (el) el.style.display = (i === cur) ? 'flex' : 'none';
    });
  },

  renderLicenseBadge() {
    const el = document.getElementById('license-badge');
    if (!this.state.license) { el.textContent = ''; el.className = ''; return; }
    const lic = this.state.license;
    el.textContent = lic.licensed ? 'Licensed' : 'Unlicensed';
    el.className = lic.status || '';
  },

  // ── Bank Bar ──────────────────────────────────────────────────────────────
  renderBankBar() {
    const bar = document.getElementById('bank-bar');
    bar.innerHTML = '';
    if (!this.state.grid) return;
    const banks = this.state.grid.banks || [];
    const cur = this.state.grid.curBank;
    for (let i = 0; i < banks.length; i++) {
      const btn = document.createElement('button');
      btn.className = 'bank-btn' + (i === cur ? ' active' : '');
      btn.textContent = banks[i].name || ('Bank ' + (i + 1));
      btn.style.borderLeftColor = banks[i].color || '#3a3a46';
      btn.addEventListener('click', () => {
        this.send({ cmd: 'selectBank', args: { bank: i } });
      });
      bar.appendChild(btn);
    }
  },

  // ── Grid ──────────────────────────────────────────────────────────────────
  bindGrid() {
    const grid = document.getElementById('grid');
    grid.addEventListener('scroll', () => {
      this.gridScrollY = grid.scrollTop;
      this.updateGridScrollbar();
    });
    grid.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      const slot = e.target.closest('.slot');
      if (slot) {
        this.showContextMenu(e.clientX, e.clientY, [
          { label: 'Open File...', cmd: 'openFile', args: { bank: parseInt(slot.dataset.bank), slot: parseInt(slot.dataset.slot) } },
          { label: 'Clear Slot', cmd: 'clearSlot', args: { bank: parseInt(slot.dataset.bank), slot: parseInt(slot.dataset.slot) } },
          { sep: true },
          { label: 'Move/Swap to...', cmd: 'armMove', args: { bank: parseInt(slot.dataset.bank), slot: parseInt(slot.dataset.slot) } },
        ]);
      }
    });
  },

  renderGrid() {
    const grid = document.getElementById('grid');
    if (!this.state.grid) return;
    const curBank = this.state.grid.curBank;
    const slots = this.state.grid.banks[curBank]?.slots || [];
    const cols = this.state.gridCols || 7;
    grid.style.gridTemplateColumns = 'repeat(' + cols + ', 1fr)';

    // Only re-render if bank changed or slot count differs
    const existing = grid.querySelectorAll('.slot');
    if (existing.length !== slots.length || grid.dataset.bank !== String(curBank)) {
      grid.innerHTML = '';
      grid.dataset.bank = curBank;
      for (let i = 0; i < slots.length; i++) {
        const slot = document.createElement('div');
        slot.className = 'slot' + (slots[i].hasFile ? ' has-file' : ' empty');
        slot.dataset.bank = curBank;
        slot.dataset.slot = i;
        if (slots[i].selected) slot.classList.add('selected');
        if (slots[i].moveSource) slot.classList.add('move-source');

        if (slots[i].thumb) {
          const img = document.createElement('img');
          img.className = 'slot-thumb';
          img.src = slots[i].thumb;
          img.alt = '';
          slot.appendChild(img);
        }
        if (slots[i].displayName) {
          const name = document.createElement('div');
          name.className = 'slot-name';
          name.textContent = slots[i].displayName;
          slot.appendChild(name);
        }
        if (slots[i].isVideo) {
          const badge = document.createElement('div');
          badge.className = 'slot-badge';
          badge.style.background = 'var(--accent)';
          slot.appendChild(badge);
        }

        slot.addEventListener('click', (e) => {
          this.send({ cmd: 'clickSlot', args: { bank: curBank, slot: i, ctrl: e.ctrlKey || e.metaKey } });
        });
        slot.addEventListener('dblclick', () => {
          this.send({ cmd: 'activateSlot', args: { bank: curBank, slot: i } });
        });
        // Drag-drop for slot move
        slot.draggable = true;
        slot.addEventListener('dragstart', (e) => {
          e.dataTransfer.setData('text/plain', JSON.stringify({ bank: curBank, slot: i }));
          slot.classList.add('dragging');
        });
        slot.addEventListener('dragend', () => { slot.classList.remove('dragging'); });
        slot.addEventListener('dragover', (e) => { e.preventDefault(); slot.classList.add('drag-over'); });
        slot.addEventListener('dragleave', () => { slot.classList.remove('drag-over'); });
        slot.addEventListener('drop', (e) => {
          e.preventDefault();
          slot.classList.remove('drag-over');
          try {
            const src = JSON.parse(e.dataTransfer.getData('text/plain'));
            this.send({ cmd: 'moveSlot', args: { srcBank: src.bank, srcSlot: src.slot, dstBank: curBank, dstSlot: i } });
          } catch (err) {}
        });

        grid.appendChild(slot);
      }
    } else {
      // Update existing slots in-place
      for (let i = 0; i < existing.length && i < slots.length; i++) {
        const el = existing[i];
        el.classList.toggle('selected', !!slots[i].selected);
        el.classList.toggle('move-source', !!slots[i].moveSource);
        el.classList.toggle('has-file', !!slots[i].hasFile);
        el.classList.toggle('empty', !slots[i].hasFile);
        // Update thumbnail
        let img = el.querySelector('.slot-thumb');
        if (slots[i].thumb && !img) {
          img = document.createElement('img');
          img.className = 'slot-thumb';
          el.insertBefore(img, el.firstChild);
        }
        if (img) img.src = slots[i].thumb;
        // Update name
        let name = el.querySelector('.slot-name');
        if (slots[i].displayName && !name) {
          name = document.createElement('div');
          name.className = 'slot-name';
          el.appendChild(name);
        }
        if (name) name.textContent = slots[i].displayName || '';
      }
    }
  },

  updateGridScrollbar() {
    const grid = document.getElementById('grid');
    const thumb = document.getElementById('grid-thumb');
    const maxScroll = grid.scrollHeight - grid.clientHeight;
    if (maxScroll <= 0) { thumb.style.display = 'none'; return; }
    thumb.style.display = 'block';
    const ratio = grid.scrollTop / maxScroll;
    const thumbH = Math.max(30, grid.clientHeight * (grid.clientHeight / grid.scrollHeight));
    thumb.style.height = thumbH + 'px';
    thumb.style.top = (ratio * (grid.clientHeight - thumbH)) + 'px';
  },

  // ── Sidebar (Media tab) ───────────────────────────────────────────────────
  bindSidebar() {
    document.querySelectorAll('#sidebar .sb-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const cmd = btn.dataset.cmd;
        if (cmd) this.send({ cmd });
      });
    });
  },

  renderSidebar() {
    if (!this.state.sidebar) return;
    const sb = this.state.sidebar;
    // Slideshow toggle
    const ssBtn = document.querySelector('[data-cmd="slideshowToggle"]');
    if (ssBtn) ssBtn.textContent = sb.ssEnabled ? 'Disable Slideshow' : 'Enable Slideshow';
    // Delay
    const delayEl = document.getElementById('delay-value');
    if (delayEl) delayEl.textContent = 'Delay ' + (sb.ssDelaySec || 5) + 's';
    // Transition
    const transBtn = document.querySelector('[data-cmd="transitionCycle"]');
    if (transBtn) {
      const modes = ['Cut', 'Fade', 'Crossfade'];
      transBtn.textContent = 'Transition: ' + (modes[sb.ssTransition] || 'Cut');
    }
    // Direction
    const dirBtn = document.querySelector('[data-cmd="directionCycle"]');
    if (dirBtn) {
      const dirs = ['Forward', 'Backward', 'Random'];
      dirBtn.textContent = 'Direction: ' + (dirs[sb.ssDirection] || 'Forward');
    }
    // Loop
    const loopBtn = document.querySelector('[data-cmd="loopToggle"]');
    if (loopBtn) loopBtn.textContent = 'Loop: ' + (sb.ssLoop ? 'On' : 'Off');
    // Scale mode
    document.querySelectorAll('[data-cmd^="scale"]').forEach(b => b.classList.remove('active'));
    const scaleMap = ['scaleAspect', 'scaleStretch', 'scaleNoscale'];
    const scaleBtn = document.querySelector('[data-cmd="' + (scaleMap[sb.scaleMode] || 'scaleAspect') + '"]');
    if (scaleBtn) scaleBtn.classList.add('active');
    // Render mode
    document.querySelectorAll('[data-cmd^="render"]').forEach(b => b.classList.remove('active'));
    const renderMap = ['renderAuto', 'renderCpu', 'renderGpu'];
    const renderBtn = document.querySelector('[data-cmd="' + (renderMap[sb.renderMode] || 'renderAuto') + '"]');
    if (renderBtn) renderBtn.classList.add('active');
  },

  // ── Layout Tab ────────────────────────────────────────────────────────────
  bindLayout() {
    document.querySelectorAll('#layout-sidebar .sb-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const cmd = btn.dataset.cmd;
        if (cmd) this.send({ cmd });
      });
    });
  },

  renderLayout() {
    if (!this.state.layout) return;
    const layout = this.state.layout;

    // Canvas panels
    const canvas = document.getElementById('layout-canvas');
    canvas.innerHTML = '';
    const panels = layout.panels || [];
    for (let i = 0; i < panels.length; i++) {
      const p = panels[i];
      if (!p.visible) continue;
      const el = document.createElement('div');
      el.className = 'layout-panel' + (i === layout.selectedPanel ? ' selected' : '');
      el.style.left = (p.x * 100) + '%';
      el.style.top = (p.y * 100) + '%';
      el.style.width = (p.w * 100) + '%';
      el.style.height = (p.h * 100) + '%';
      el.dataset.index = i;

      const label = document.createElement('div');
      label.className = 'layout-panel-label';
      const typeNames = ['Main', 'Image', 'Text', 'Shader', 'Song'];
      label.textContent = typeNames[p.panelType] || 'Panel';
      el.appendChild(label);

      if (p.thumb) {
        const img = document.createElement('img');
        img.style.width = '100%';
        img.style.height = '100%';
        img.style.objectFit = p.imageFitMode === 1 ? 'cover' : 'contain';
        img.src = p.thumb;
        el.appendChild(img);
      }

      if (i === layout.selectedPanel) {
        const resize = document.createElement('div');
        resize.className = 'layout-panel-resize';
        el.appendChild(resize);
      }

      el.addEventListener('mousedown', (e) => this.onPanelMouseDown(e, i));
      canvas.appendChild(el);
    }

    // Panel list
    const list = document.getElementById('panel-list');
    list.innerHTML = '';
    for (let i = 0; i < panels.length; i++) {
      const p = panels[i];
      const item = document.createElement('div');
      item.className = 'panel-item' + (i === layout.selectedPanel ? ' selected' : '');
      const color = document.createElement('div');
      color.className = 'panel-item-color';
      const typeColors = ['#0078d7', '#2ecc71', '#f1c40f', '#e74c3c', '#9b59b6'];
      color.style.background = typeColors[p.panelType] || '#3a3a46';
      const name = document.createElement('div');
      name.className = 'panel-item-name';
      const typeNames = ['Main Slideshow', 'Image', 'Text', 'Shader', 'Song / Now Playing'];
      name.textContent = typeNames[p.panelType] || 'Panel';
      item.appendChild(color);
      item.appendChild(name);
      item.addEventListener('click', () => {
        this.send({ cmd: 'selectPanel', args: { index: i } });
      });
      list.appendChild(item);
    }

    // Direct select toggle
    const dsBtn = document.querySelector('[data-cmd="directSelect"]');
    if (dsBtn) dsBtn.textContent = 'Canvas Select: ' + (layout.directSelectEnabled ? 'ON' : 'OFF');

    // Properties
    this.renderPanelProperties(layout);
  },

  onPanelMouseDown(e, index) {
    e.preventDefault();
    e.stopPropagation();
    const layout = this.state.layout;
    const panel = layout.panels[index];
    const canvas = document.getElementById('layout-canvas');
    const rect = canvas.getBoundingClientRect();

    const isResize = e.target.classList.contains('layout-panel-resize');
    this.dragState = {
      index,
      mode: isResize ? 'resize' : 'move',
      startX: e.clientX,
      startY: e.clientY,
      origX: panel.x,
      origY: panel.y,
      origW: panel.w,
      origH: panel.h,
      canvasW: rect.width,
      canvasH: rect.height,
    };

    if (!isResize) this.send({ cmd: 'selectPanel', args: { index } });

    const onMove = (ev) => {
      if (!this.dragState) return;
      const ds = this.dragState;
      const dx = (ev.clientX - ds.startX) / ds.canvasW;
      const dy = (ev.clientY - ds.startY) / ds.canvasH;
      if (ds.mode === 'move') {
        this.send({ cmd: 'movePanel', args: {
          index: ds.index,
          x: Math.max(0, Math.min(1 - ds.origW, ds.origX + dx)),
          y: Math.max(0, Math.min(1 - ds.origH, ds.origY + dy)),
        }});
      } else {
        this.send({ cmd: 'resizePanel', args: {
          index: ds.index,
          w: Math.max(0.05, Math.min(1 - ds.origX, ds.origW + dx)),
          h: Math.max(0.05, Math.min(1 - ds.origY, ds.origH + dy)),
        }});
      }
    };
    const onUp = () => {
      this.dragState = null;
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
    };
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  },

  renderPanelProperties(layout) {
    const propsEl = document.getElementById('panel-properties');
    const content = document.getElementById('props-content');
    if (layout.selectedPanel < 0 || !layout.panels[layout.selectedPanel]) {
      propsEl.style.display = 'none';
      return;
    }
    propsEl.style.display = 'block';
    const p = layout.panels[layout.selectedPanel];
    content.innerHTML = '';

    // X / Y / W / H
    const fields = [
      { label: 'X', key: 'x', step: 0.01 },
      { label: 'Y', key: 'y', step: 0.01 },
      { label: 'W', key: 'w', step: 0.01 },
      { label: 'H', key: 'h', step: 0.01 },
    ];
    fields.forEach(f => {
      const row = document.createElement('div');
      row.className = 'prop-row';
      const lbl = document.createElement('div');
      lbl.className = 'prop-label';
      lbl.textContent = f.label;
      const input = document.createElement('input');
      input.className = 'prop-input';
      input.type = 'number';
      input.step = f.step;
      input.value = (p[f.key] || 0).toFixed(2);
      input.addEventListener('change', () => {
        this.send({ cmd: 'setPanelProperty', args: { index: layout.selectedPanel, prop: f.key, value: parseFloat(input.value) } });
      });
      row.appendChild(lbl);
      row.appendChild(input);
      content.appendChild(row);
    });

    // Panel-type-specific properties
    if (p.panelType === 1) {
      // Image panel
      const row = document.createElement('div');
      row.className = 'prop-row';
      const btn = document.createElement('button');
      btn.className = 'sb-btn';
      btn.textContent = p.hasImage ? 'Change Image...' : 'Load Image...';
      btn.addEventListener('click', () => {
        this.send({ cmd: 'loadPanelImage', args: { index: layout.selectedPanel } });
      });
      row.appendChild(btn);
      content.appendChild(row);
    } else if (p.panelType === 2) {
      // Text panel
      const row = document.createElement('div');
      row.className = 'prop-row';
      const lbl = document.createElement('div');
      lbl.className = 'prop-label';
      lbl.textContent = 'Text';
      const input = document.createElement('input');
      input.className = 'prop-input';
      input.value = p.textContent || '';
      input.addEventListener('change', () => {
        this.send({ cmd: 'setPanelProperty', args: { index: layout.selectedPanel, prop: 'textContent', value: input.value } });
      });
      row.appendChild(lbl);
      row.appendChild(input);
      content.appendChild(row);
    } else if (p.panelType === 3) {
      // Shader panel
      const row = document.createElement('div');
      row.className = 'prop-row';
      const lbl = document.createElement('div');
      lbl.className = 'prop-label';
      lbl.textContent = 'Shader';
      const sel = document.createElement('select');
      sel.className = 'prop-select';
      const shaders = this.state.shaders || [];
      shaders.forEach((s, i) => {
        const opt = document.createElement('option');
        opt.value = i;
        opt.textContent = s;
        if (i === p.shaderIndex) opt.selected = true;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        this.send({ cmd: 'setPanelProperty', args: { index: layout.selectedPanel, prop: 'shaderIndex', value: parseInt(sel.value) } });
      });
      row.appendChild(lbl);
      row.appendChild(sel);
      content.appendChild(row);
    }
  },

  // ── Options Tab ───────────────────────────────────────────────────────────
  bindOptions() {
    document.querySelectorAll('#tab-options .sb-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const cmd = btn.dataset.cmd;
        if (cmd) this.send({ cmd });
      });
    });
  },

  renderOptions() {
    if (!this.state.sidebar) return;
    const sb = this.state.sidebar;
    // Scale mode
    document.querySelectorAll('[data-cmd^="optScale"]').forEach(b => b.classList.remove('active'));
    const scaleMap = ['optScaleAspect', 'optScaleStretch', 'optScaleNoscale'];
    const scaleBtn = document.querySelector('[data-cmd="' + (scaleMap[sb.scaleMode] || 'optScaleAspect') + '"]');
    if (scaleBtn) scaleBtn.classList.add('active');
    // Transition
    document.querySelectorAll('[data-cmd^="optTrans"]').forEach(b => b.classList.remove('active'));
    const transMap = ['optTransCut', 'optTransFade', 'optTransCross'];
    const transBtn = document.querySelector('[data-cmd="' + (transMap[sb.ssTransition] || 'optTransCut') + '"]');
    if (transBtn) transBtn.classList.add('active');
    // Direction
    document.querySelectorAll('[data-cmd^="optDir"]').forEach(b => b.classList.remove('active'));
    const dirMap = ['optDirFwd', 'optDirBack', 'optDirRandom'];
    const dirBtn = document.querySelector('[data-cmd="' + (dirMap[sb.ssDirection] || 'optDirFwd') + '"]');
    if (dirBtn) dirBtn.classList.add('active');
    // Loop
    const loopBtn = document.querySelector('[data-cmd="optLoopToggle"]');
    if (loopBtn) loopBtn.textContent = 'Loop: ' + (sb.ssLoop ? 'On' : 'Off');
    // Delay
    const delayEl = document.getElementById('opt-delay-value');
    if (delayEl) delayEl.textContent = (sb.ssDelaySec || 5) + 's';
    // Shaders
    const shaderBtn = document.querySelector('[data-cmd="optShaderEnable"]');
    if (shaderBtn) shaderBtn.textContent = 'Enable Shaders: ' + (sb.shadersEnabled ? 'On' : 'Off');
    const karaokeBtn = document.querySelector('[data-cmd="optShaderKaraoke"]');
    if (karaokeBtn) karaokeBtn.textContent = 'Disable on Karaoke: ' + (sb.shaderDisableOnKaraoke ? 'On' : 'Off');
    const shaderName = document.getElementById('opt-shader-name');
    if (shaderName) {
      const shaders = this.state.shaders || [];
      shaderName.textContent = shaders[sb.mainShaderIndex] || 'None';
    }
    // Karaoke auto-hide
    const autohideBtn = document.querySelector('[data-cmd="optKaraokeAutohide"]');
    if (autohideBtn) autohideBtn.textContent = 'Disable Panels/Slideshow on Karaoke: ' + (this.state.karaokeAutoHide ? 'On' : 'Off');
  },

  // ── License Tab ───────────────────────────────────────────────────────────
  bindLicense() {
    document.querySelectorAll('#tab-license .sb-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const cmd = btn.dataset.cmd;
        if (!cmd) return;
        if (cmd === 'licenseLogin') {
          const email = document.getElementById('license-email').value;
          const password = document.getElementById('license-password').value;
          const save = document.getElementById('license-save').checked;
          this.send({ cmd, args: { email, password, save } });
        } else {
          this.send({ cmd });
        }
      });
    });
  },

  renderLicense() {
    if (!this.state.license) return;
    const lic = this.state.license;
    const badge = document.getElementById('license-status-badge');
    const details = document.getElementById('license-details');
    const statusText = document.getElementById('license-status-text');
    const logoutRow = document.getElementById('license-logout-row');

    const statusLabels = {
      LIC_UNCHECKED: 'Not Checked',
      LIC_VALID: 'Valid',
      LIC_EXPIRED: 'Expired',
      LIC_INVALID: 'Invalid',
      LIC_REVOKED: 'Revoked',
      LIC_ERROR: 'Error',
    };
    const statusClasses = {
      LIC_VALID: 'valid',
      LIC_EXPIRED: 'expired',
      LIC_INVALID: 'invalid',
      LIC_REVOKED: 'invalid',
    };

    badge.textContent = statusLabels[lic.status] || 'Unknown';
    badge.className = statusClasses[lic.status] || '';

    let html = '';
    if (lic.licenseType) html += '<div><strong>Type:</strong> ' + lic.licenseType + '</div>';
    if (lic.userEmail) html += '<div><strong>Email:</strong> ' + lic.userEmail + '</div>';
    if (lic.licExpiry) html += '<div><strong>Expires:</strong> ' + lic.licExpiry + '</div>';
    if (lic.maxActivations) html += '<div><strong>Activations:</strong> ' + lic.currentActivations + '/' + lic.maxActivations + '</div>';
    details.innerHTML = html;

    statusText.textContent = lic.message || '';
    logoutRow.style.display = lic.licensed ? 'block' : 'none';

    // Pre-fill email if saved
    const emailInput = document.getElementById('license-email');
    if (lic.userEmail && !emailInput.value) emailInput.value = lic.userEmail;
  },

  // ── Status Bar ────────────────────────────────────────────────────────────
  renderStatus() {
    const el = document.getElementById('status-text');
    if (el && this.state.status) el.textContent = this.state.status;
  },

  // ── Event handling ────────────────────────────────────────────────────────
  handleEvent(name, data) {
    if (name === 'nowPlaying') this.updateNowPlaying(data);
    else if (name === 'renderMode') {
      if (this.state) this.state.status = data;
      this.renderStatus();
    }
  },

  updateNowPlaying(text) {
    if (this.state) this.state.status = text;
    this.renderStatus();
  },

  applyThumbnail(slot, bank, data) {
    if (!this.state || !this.state.grid) return;
    if (bank !== this.state.grid.curBank) return;
    const slots = this.state.grid.banks[bank]?.slots;
    if (!slots || slot >= slots.length) return;
    slots[slot].thumb = data;
    // Update DOM in-place
    const el = document.querySelector('.slot[data-bank="' + bank + '"][data-slot="' + slot + '"]');
    if (el) {
      let img = el.querySelector('.slot-thumb');
      if (!img) {
        img = document.createElement('img');
        img.className = 'slot-thumb';
        el.insertBefore(img, el.firstChild);
      }
      img.src = data;
    }
  },

  applyPanelImage(index, data) {
    if (!this.state || !this.state.layout) return;
    const panels = this.state.layout.panels;
    if (index >= 0 && index < panels.length) {
      panels[index].thumb = data;
      this.renderLayout();
    }
  },

  // ── Context Menu ──────────────────────────────────────────────────────────
  showContextMenu(x, y, items) {
    const existing = document.querySelector('.context-menu');
    if (existing) existing.remove();
    const menu = document.createElement('div');
    menu.className = 'context-menu';
    menu.style.left = x + 'px';
    menu.style.top = y + 'px';
    items.forEach(item => {
      if (item.sep) {
        const sep = document.createElement('div');
        sep.className = 'context-sep';
        menu.appendChild(sep);
      } else {
        const el = document.createElement('div');
        el.className = 'context-item';
        el.textContent = item.label;
        el.addEventListener('click', () => {
          menu.remove();
          this.send({ cmd: item.cmd, args: item.args || {} });
        });
        menu.appendChild(el);
      }
    });
    document.body.appendChild(menu);
    const close = (e) => {
      if (!menu.contains(e.target)) { menu.remove(); document.removeEventListener('mousedown', close); }
    };
    setTimeout(() => document.addEventListener('mousedown', close), 0);
  },

  // ── Keyboard ──────────────────────────────────────────────────────────────
  bindKeyboard() {
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') {
        const menu = document.querySelector('.context-menu');
        if (menu) { menu.remove(); return; }
        this.send({ cmd: 'cancelMove' });
      }
    });
  },
};

// ── Entry point ─────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => App.init());

// Expose for native code
window.tmOnMessage = (json) => App.onMessage(json);

/* Workspace-only prototype interactions. Separate from inspector parameter bindings.
   Layout handles map to SSplitter; viewport panels map to the existing overlay controls. */
'use strict';
(() => {
  const ui = window.MixtormatPrototype;
  const root = document.documentElement;
  const frame = document.querySelector('.workspace');
  const grid = document.querySelector('.main-grid');
  const center = document.querySelector('.center-panel');
  const viewport = document.getElementById('viewport');
  const cssNumber = name => parseFloat(getComputedStyle(root).getPropertyValue(name));
  const clamp = (value, low, high) => Math.max(low, Math.min(Math.max(low, high), value));
  const setToken = (name, value) => {
    root.style.setProperty(name, `${value}px`);
    const input = document.getElementById(`token-${name.slice(2)}`);
    if (input) input.value = Math.round(value);
  };
  const png = (name, className = 'asset-icon') => {
    const image = ui.node('img', className);
    image.src = `icons/${name}.png`; image.alt = ''; return image;
  };

  // Shared pointer transaction: capture, release, cancellation, and keyboard-friendly handles.
  function pointerDrag(handle, begin, move, end = () => {}) {
    handle.addEventListener('pointerdown', event => {
      if (event.button !== 0 || event.target.closest('button, input, select')) return;
      const context = begin(event);
      if (!context) return;
      event.preventDefault(); handle.setPointerCapture(event.pointerId);
      document.body.classList.add('resizing'); handle.classList.add('dragging');
      const track = next => move(next, context);
      const stop = next => {
        handle.removeEventListener('pointermove', track);
        handle.removeEventListener('pointerup', stop);
        handle.removeEventListener('pointercancel', stop);
        handle.removeEventListener('lostpointercapture', stop);
        document.body.classList.remove('resizing'); handle.classList.remove('dragging');
        end(next, context);
      };
      handle.addEventListener('pointermove', track);
      handle.addEventListener('pointerup', stop);
      handle.addEventListener('pointercancel', stop);
      handle.addEventListener('lostpointercapture', stop);
    });
  }
  function splitter(axis, label, value, update, bounds) {
    const handle = ui.node('div', 'splitter'); handle.dataset.axis = axis;
    handle.dataset.slate = 'SSplitter'; handle.tabIndex = 0;
    handle.setAttribute('role', 'separator'); handle.setAttribute('aria-label', label);
    handle.setAttribute('aria-orientation', axis === 'x' ? 'vertical' : 'horizontal');
    handle.title = `${label} · drag or use arrow keys`;
    const write = next => {
      const [low, high] = bounds(); const size = clamp(next, low, high);
      update(size); handle.setAttribute('aria-valuenow', Math.round(size));
      handle.setAttribute('aria-valuemin', low); handle.setAttribute('aria-valuemax', Math.round(Math.max(low, high)));
    };
    pointerDrag(handle, event => ({ start: axis === 'x' ? event.clientX : event.clientY, value: value() }), (event, context) => {
      const current = axis === 'x' ? event.clientX : event.clientY;
      write(context.value + (current - context.start) * (label.includes('Inspector') || axis === 'y' ? -1 : 1));
    });
    handle.addEventListener('keydown', event => {
      const keys = axis === 'x' ? ['ArrowLeft', 'ArrowRight'] : ['ArrowUp', 'ArrowDown'];
      if (!keys.includes(event.key)) return;
      event.preventDefault();
      const sign = event.key === keys[0] ? -1 : 1;
      write(value() + sign * (event.shiftKey ? 1 : 10));
    });
    handle.setAttribute('aria-valuenow', Math.round(value()));
    return handle;
  }
  const leftSplitter = splitter('x', 'Layer column width', () => cssNumber('--left-width'), value => setToken('--left-width', value), () => [160, Math.min(640, grid.clientWidth - cssNumber('--inspector-width') - 222)]);
  const rightSplitter = splitter('x', 'Inspector column width', () => cssNumber('--inspector-width'), value => setToken('--inspector-width', value), () => [200, Math.min(720, grid.clientWidth - cssNumber('--left-width') - 222)]);
  grid.insertBefore(leftSplitter, center);
  grid.insertBefore(rightSplitter, document.querySelector('.inspector'));
  const gallerySplitter = splitter('y', 'Gallery height', () => cssNumber('--gallery-height'), value => {
    center.classList.remove('gallery-collapsed');
    document.getElementById('galleryGrid').hidden = false;
    document.querySelector('[data-action=gallery]').textContent = 'Collapse';
    setToken('--gallery-height', value);
  }, () => [80, center.clientHeight - document.querySelector('.preview-toolbar').clientHeight - 151]);
  center.insertBefore(gallerySplitter, document.getElementById('gallery'));

  // Floating-window reference. It cannot resize the real browser/Unreal OS window.
  function floatFrame() {
    if (frame.classList.contains('floating')) return;
    const box = frame.getBoundingClientRect();
    frame.classList.add('floating');
    Object.assign(frame.style, { left: `${box.left}px`, top: `${box.top}px`, width: `${box.width}px`, height: `${box.height}px` });
  }
  document.querySelectorAll('.topbar .brand, .topbar .document-name').forEach(caption => {
    pointerDrag(caption, event => {
      floatFrame(); const box = frame.getBoundingClientRect();
      return { x: event.clientX, y: event.clientY, left: box.left, top: box.top };
    }, (event, start) => {
      frame.style.left = `${clamp(start.left + event.clientX - start.x, -frame.clientWidth + 120, window.innerWidth - 120)}px`;
      frame.style.top = `${clamp(start.top + event.clientY - start.y, 0, window.innerHeight - 40)}px`;
    });
    caption.addEventListener('dblclick', () => {
      frame.classList.remove('floating');
      ['left', 'top', 'width', 'height'].forEach(name => frame.style.removeProperty(name));
    });
  });
  const grip = ui.node('div', 'window-grip'); grip.tabIndex = 0;
  grip.setAttribute('role', 'button'); grip.setAttribute('aria-label', 'Resize prototype window; double-click the title to restore');
  grip.title = 'Drag to resize prototype window · double-click title restores full window';
  grip.append(png('grip')); frame.append(grip);
  function frameSize(width, height) {
    const box = frame.getBoundingClientRect();
    frame.style.width = `${clamp(width, Math.min(760, window.innerWidth), Math.max(760, window.innerWidth - Math.max(box.left, 0)))}px`;
    frame.style.height = `${clamp(height, Math.min(480, window.innerHeight), Math.max(480, window.innerHeight - box.top))}px`;
  }
  pointerDrag(grip, event => {
    floatFrame(); const box = frame.getBoundingClientRect();
    return { x: event.clientX, y: event.clientY, width: box.width, height: box.height };
  }, (event, start) => frameSize(start.width + event.clientX - start.x, start.height + event.clientY - start.y));
  grip.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) return;
    event.preventDefault(); floatFrame();
    frameSize(frame.clientWidth + (event.key === 'ArrowLeft' ? -10 : event.key === 'ArrowRight' ? 10 : 0), frame.clientHeight + (event.key === 'ArrowUp' ? -10 : event.key === 'ArrowDown' ? 10 : 0));
  });

  // The style editor floats independently, leaving the preview and inspector reachable.
  const stylePopup = document.getElementById('styleDrawer');
  const styleTitle = stylePopup.querySelector('header');
  styleTitle.tabIndex = 0;
  styleTitle.setAttribute('aria-label', 'Move UI Style popup; drag or use arrow keys');
  styleTitle.title = 'Drag to move · arrow keys move · double-click restores size and position';
  function stylePosition(left, top) {
    stylePopup.style.right = 'auto';
    stylePopup.style.left = `${clamp(left, 0, window.innerWidth - stylePopup.offsetWidth)}px`;
    stylePopup.style.top = `${clamp(top, 0, window.innerHeight - stylePopup.offsetHeight)}px`;
  }
  pointerDrag(styleTitle, event => {
    const box = stylePopup.getBoundingClientRect();
    return { x: event.clientX, y: event.clientY, left: box.left, top: box.top };
  }, (event, start) => stylePosition(start.left + event.clientX - start.x, start.top + event.clientY - start.y));
  styleTitle.addEventListener('keydown', event => {
    if (event.target !== styleTitle || !['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) return;
    event.preventDefault();
    const box = stylePopup.getBoundingClientRect();
    stylePosition(box.left + (event.key === 'ArrowLeft' ? -10 : event.key === 'ArrowRight' ? 10 : 0), box.top + (event.key === 'ArrowUp' ? -10 : event.key === 'ArrowDown' ? 10 : 0));
  });
  styleTitle.addEventListener('dblclick', event => {
    if (event.target.closest('button')) return;
    ['left', 'top', 'right', 'width', 'height'].forEach(name => stylePopup.style.removeProperty(name));
  });
  const styleGrip = ui.node('div', 'window-grip style-resize-grip');
  styleGrip.tabIndex = 0;
  styleGrip.setAttribute('role', 'button');
  styleGrip.setAttribute('aria-label', 'Resize UI Style popup; drag or use arrow keys');
  styleGrip.title = 'Drag to resize UI Style popup';
  styleGrip.append(png('grip')); stylePopup.append(styleGrip);
  function styleSize(width, height) {
    const box = stylePopup.getBoundingClientRect();
    stylePosition(box.left, box.top);
    stylePopup.style.width = `${clamp(width, Math.min(280, window.innerWidth), window.innerWidth - box.left)}px`;
    stylePopup.style.height = `${clamp(height, Math.min(240, window.innerHeight), window.innerHeight - box.top)}px`;
    stylePosition(box.left, box.top);
  }
  pointerDrag(styleGrip, event => {
    const box = stylePopup.getBoundingClientRect();
    return { x: event.clientX, y: event.clientY, width: box.width, height: box.height };
  }, (event, start) => styleSize(start.width + event.clientX - start.x, start.height + event.clientY - start.y));
  styleGrip.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) return;
    event.preventDefault();
    styleSize(stylePopup.offsetWidth + (event.key === 'ArrowLeft' ? -10 : event.key === 'ArrowRight' ? 10 : 0), stylePopup.offsetHeight + (event.key === 'ArrowUp' ? -10 : event.key === 'ArrowDown' ? 10 : 0));
  });
  window.addEventListener('resize', () => {
    if (stylePopup.hidden) return;
    const box = stylePopup.getBoundingClientRect();
    stylePosition(box.left, box.top);
  });

  // Replace static glyphs with the actual plugin PNGs. Dynamic icons use app.js's factory.
  document.querySelector('[data-action=add-layer]').replaceChildren(png('add'));
  document.querySelector('[data-action=debug]').replaceChildren(png('eye-off'));
  document.querySelector('[data-action=frame]').replaceChildren(png('camera'));
  document.querySelector('[data-action=wire]').replaceChildren(png('nodes'));
  const iconActions = { new: 'add', load: 'folder', save: 'save', export: 'save-as', style: 'settings', bake: 'quality-high', contract: 'documentation' };
  Object.entries(iconActions).forEach(([action, file]) => {
    const button = document.querySelector(`.top-actions [data-action=${action}]`);
    button.prepend(png(file)); button.classList.add('button-with-icon');
  });
  const moduleIcon = { MASK: 'mask', EFFECT: 'effect', GEN: 'generator', IDS: 'ids', OUT: 'nodes' };
  function layerIcons() {
    document.querySelectorAll('.layer-row.child').forEach(row => {
      if (row.querySelector('.layer-module-icon')) return;
      const source = row.querySelector('.layer-source')?.textContent;
      row.insertBefore(png(moduleIcon[source] || 'generated', 'layer-module-icon'), row.querySelector('.layer-name'));
    });
  }
  layerIcons();
  new MutationObserver(layerIcons).observe(document.getElementById('layerTree'), { childList: true });

  // Source-aligned OUTPUT / SCENE / CAMERA overlay component examples.
  const overlay = ui.node('section', 'viewport-overlay'); overlay.dataset.slate = 'SMixtormat viewport overlays';
  overlay.setAttribute('aria-label', 'Preview overlay controls');
  const title = ui.node('div', 'overlay-title', 'PREVIEW CONTROLS');
  title.append(png('grip')); overlay.append(title);
  const tabs = ui.node('div', 'overlay-tabs'); overlay.append(tabs);
  const definitions = [
    { title: 'OUTPUT', image: 'nodes', rows: [
      ui.dropdown({ label: 'Resolution', options: ['1K', '2K', '4K'], selected: 1 }, 'overlay.resolution'),
      ui.segments({ options: ['DEFAULT', 'LUMEN'] }, 'overlay.quality'),
      ui.segments({ options: ['FXAA', 'TSR'] }, 'overlay.aa'),
      ui.dragger({ label: 'Scale', value: 150, min: 50, max: 200, integer: true }, 'overlay.scale')
    ] },
    { title: 'SCENE', image: 'light-neutral', rows: [
      ui.toggle({ label: 'Displacement', value: true }, 'overlay.displacement'),
      ui.dragger({ label: 'Amount', value: 1, min: 0, max: 4 }, 'overlay.amount'),
      ui.dragger({ label: 'Light', value: .8, min: 0, max: 2 }, 'overlay.light'),
      ui.dragger({ label: 'Skylight', value: .1, min: 0, max: 2 }, 'overlay.skylight')
    ] },
    { title: 'CAMERA', image: 'camera', rows: [
      ui.dragger({ label: 'FOV', value: 45, min: 15, max: 100 }, 'overlay.fov'),
      ui.dropdown({ label: 'Preset', options: ['Neutral', 'Soft', 'Dramatic', 'Rim'], selected: 0 }, 'overlay.preset')
    ] }
  ];
  const panels = [];
  definitions.forEach((definition, index) => {
    const button = ui.node('button', '', definition.title);
    button.prepend(png(definition.image)); button.setAttribute('aria-pressed', String(index === 0));
    const panel = ui.node('div', 'overlay-controls'); panel.hidden = index !== 0;
    panel.append(...definition.rows, ui.node('div', 'overlay-note', 'Reference controls · simulated rendering only'));
    const panelId = `overlay-panel-${index}`; panel.id = panelId; button.setAttribute('aria-controls', panelId);
    panels.push(panel);
    button.addEventListener('click', () => {
      const open = panel.hidden;
      panels.forEach(item => { item.hidden = true; });
      tabs.querySelectorAll('button').forEach(item => item.setAttribute('aria-pressed', 'false'));
      panel.hidden = !open; button.setAttribute('aria-pressed', String(open));
    });
    tabs.append(button); overlay.append(panel);
  });
  viewport.append(overlay);
  pointerDrag(title, event => ({ x: event.clientX, y: event.clientY, left: overlay.offsetLeft, top: overlay.offsetTop }), (event, start) => {
    overlay.style.left = `${clamp(start.left + event.clientX - start.x, 0, viewport.clientWidth - overlay.offsetWidth)}px`;
    overlay.style.top = `${clamp(start.top + event.clientY - start.y, 0, viewport.clientHeight - overlay.offsetHeight)}px`;
  });

  // All copied icons can be inspected without invoking or changing Unreal functionality.
  const iconNames = ['add', 'arrow-down', 'arrow-up', 'camera', 'check', 'chevron-down-bold', 'chevron-down', 'chevron-right', 'chevron-up', 'cube', 'cylinder', 'documentation', 'duplicate', 'effect', 'eye-off', 'eye', 'feedback', 'folder', 'generated', 'generator', 'globe', 'grip', 'hierarchy-root', 'ids', 'indent-1', 'indent-2', 'indent-3', 'layer-fill', 'layer-material', 'light-dramatic', 'light-neutral', 'light-rim', 'light-soft', 'mask', 'nodes', 'overflow', 'plane', 'quality-high', 'quality-low', 'quality-medium', 'ramp-bspline', 'ramp-constant', 'ramp-frame', 'ramp-linear', 'ramp-reset', 'ramp-spline', 'refresh', 'save-as', 'save', 'search', 'settings', 'sphere', 'trash', 'tree-branch-dotted', 'tree-cross', 'tree-elbow', 'tree-tee'];
  const sheetButton = ui.node('button', '', 'Plugin PNG icon sheet');
  document.querySelector('#styleDrawer footer').append(sheetButton);
  sheetButton.addEventListener('click', () => {
    const content = document.getElementById('modalContent'); content.replaceChildren(ui.node('h2', '', '57 source PNG icons'));
    const sheet = ui.node('div', 'icon-sheet');
    iconNames.forEach(name => {
      const cell = ui.node('div'); cell.append(png(name), ui.node('span', '', name)); sheet.append(cell);
    });
    content.append(sheet); document.getElementById('modal').showModal();
  });
})();

/* Prototype behavior only. No Unreal calls, remote assets, or persistent storage.
   Component creation, demo state/history, and token editing are separate sections. */
'use strict';
(() => {
  const data = window.MixtormatPrototypeData;
  const root = document.documentElement;
  const byId = id => document.getElementById(id);
  const state = { inspector: 'surface', gallery: 'materials', parameters: new Map(), undo: [], redo: [], dirty: false };
  const status = message => { byId('status').textContent = message; };
  const node = (tag, className, text) => {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (text !== undefined) element.textContent = text;
    return element;
  };
  const iconFiles = { eye: 'eye', reset: 'refresh', debug: 'eye-off', plus: 'add', link: 'nodes' };
  const icon = (name, label, action) => {
    const button = node('button', 'icon-button');
    button.type = 'button';
    button.title = label;
    button.setAttribute('aria-label', label);
    if (action) button.dataset.action = action;
    const image = node('img', 'asset-icon');
    image.src = `icons/${iconFiles[name] || name}.png`;
    image.alt = '';
    button.append(image);
    return button;
  };

  // --- Parameter model / history ------------------------------------------------------------
  function parameter(key, initial, defaultValue = initial) {
    if (!state.parameters.has(key)) state.parameters.set(key, { value: initial, defaultValue });
    return state.parameters.get(key);
  }
  function history(changes) {
    const actual = changes.filter(change => change.before !== change.after);
    if (!actual.length) return;
    state.undo.push(actual);
    state.redo.length = 0;
    state.dirty = true;
    syncHistory();
  }
  function syncHistory() {
    byId('undoButton').disabled = !state.undo.length;
    byId('redoButton').disabled = !state.redo.length;
    byId('dirtyMark').textContent = state.dirty ? 'EDITED · DEMO' : 'DEMO';
  }
  function applyHistory(direction) {
    const from = direction === 'undo' ? state.undo : state.redo;
    const to = direction === 'undo' ? state.redo : state.undo;
    if (!from.length) return;
    const changes = from.pop();
    changes.forEach(change => { state.parameters.get(change.key).value = direction === 'undo' ? change.before : change.after; });
    to.push(changes);
    syncHistory();
    renderInspector();
    status(`${direction === 'undo' ? 'Undo' : 'Redo'}: prototype parameter edit`);
  }

  // --- Shared controls --------------------------------------------------------------------
  function dragger(spec, key) {
    const model = parameter(key, spec.value, spec.defaultValue ?? spec.value);
    const row = node('div', 'dragger well');
    row.dataset.slate = 'SMixtormatSlider';
    row.dataset.parameter = key;
    row.setAttribute('role', 'slider');
    row.setAttribute('aria-label', spec.label);
    row.setAttribute('aria-valuemin', spec.min);
    row.setAttribute('aria-valuemax', spec.max);
    row.tabIndex = spec.disabled ? -1 : 0;
    row.classList.toggle('disabled', !!spec.disabled);
    row.setAttribute('aria-disabled', String(!!spec.disabled));
    const fill = node('span', 'drag-fill');
    const shade = node('span', 'drag-shade');
    const label = node('span', 'drag-label', spec.label);
    const value = node('span', 'drag-value');
    row.append(fill, shade, label, value);
    if (spec.min < 0 && spec.max > 0) {
      row.classList.add('signed');
      row.append(node('span', 'zero-tick'));
    }
    const clamp = number => Math.max(spec.min, Math.min(spec.max, spec.integer ? Math.round(number) : number));
    function paint() {
      value.textContent = spec.integer ? String(Math.round(model.value)) : Number(model.value).toFixed(3);
      row.setAttribute('aria-valuenow', model.value);
      row.classList.toggle('modified', Math.abs(model.value - model.defaultValue) > .000001);
      const fraction = (model.value - spec.min) / (spec.max - spec.min);
      const origin = spec.min < 0 && spec.max > 0 ? -spec.min / (spec.max - spec.min) : 0;
      const left = `${Math.min(fraction, origin) * 100}%`;
      const width = `${Math.abs(fraction - origin) * 100}%`;
      // Both fill passes share one geometry, so they stay registered while scrubbing.
      [fill, shade].forEach(layer => {
        layer.style.left = left; layer.style.width = width; layer.style.right = 'auto';
      });
    }
    function set(number) { model.value = clamp(number); paint(); }
    function reset() {
      const before = model.value;
      set(model.defaultValue);
      history([{ key, before, after: model.value }]);
    }
    row.title = `${spec.label} · drag · double-click/Enter to type · arrows to adjust · Shift fine · Backspace reset`;
    row.addEventListener('pointerdown', event => {
      if (spec.disabled || event.target.tagName === 'INPUT' || event.button !== 0) return;
      const before = model.value;
      const start = event.clientX;
      row.setPointerCapture(event.pointerId);
      row.classList.add('dragging');
      const move = next => {
        const sensitivity = next.shiftKey ? .001 : .01;
        set(before + (next.clientX - start) * (spec.max - spec.min) * sensitivity);
      };
      const end = () => {
        row.removeEventListener('pointermove', move);
        row.removeEventListener('pointerup', end);
        row.removeEventListener('pointercancel', end);
        row.removeEventListener('lostpointercapture', end);
        row.classList.remove('dragging');
        history([{ key, before, after: model.value }]);
      };
      row.addEventListener('pointermove', move);
      row.addEventListener('pointerup', end);
      row.addEventListener('pointercancel', end);
      row.addEventListener('lostpointercapture', end);
    });
    function edit() {
      if (spec.disabled || row.querySelector('input')) return;
      const input = node('input');
      input.type = 'number'; input.value = model.value;
      input.step = spec.integer ? '1' : '0.001';
      input.setAttribute('aria-label', `${spec.label} numeric value`);
      const before = model.value;
      let closed = false;
      const commit = cancel => {
        if (closed) return;
        closed = true;
        if (!cancel && Number.isFinite(input.valueAsNumber)) set(input.valueAsNumber);
        input.remove();
        history([{ key, before, after: model.value }]);
        row.focus();
      };
      input.addEventListener('keydown', event => {
        event.stopPropagation();
        if (event.key === 'Enter') commit(false);
        if (event.key === 'Escape') commit(true);
      });
      input.addEventListener('blur', () => commit(false));
      row.append(input); input.focus(); input.select();
    }
    row.addEventListener('prototype:reset', () => { if (!spec.disabled) reset(); });
    row.addEventListener('dblclick', edit);
    row.addEventListener('keydown', event => {
      if (spec.disabled || event.target.tagName === 'INPUT') return;
      if (event.key === 'Enter') { event.preventDefault(); edit(); return; }
      if (event.key === 'Backspace' || event.key === 'Delete') { event.preventDefault(); reset(); return; }
      if (!['ArrowLeft', 'ArrowDown', 'ArrowRight', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      const before = model.value;
      const delta = spec.integer ? 1 : (spec.max - spec.min) * (event.shiftKey ? .001 : .01);
      set(event.key === 'Home' ? spec.min : event.key === 'End' ? spec.max : model.value + (['ArrowLeft', 'ArrowDown'].includes(event.key) ? -delta : delta));
      history([{ key, before, after: model.value }]);
    });
    paint();
    return row;
  }
  function dropdown(spec, key) {
    const model = parameter(key, spec.selected);
    const row = node('div', 'dropdown-row'); row.dataset.slate = 'MixtormatRow::MakeDropdown';
    const label = node('label', '', spec.label);
    const well = node('div', 'well');
    const select = node('select'); select.id = `param-${key.replace(/[^a-z0-9]/gi, '-')}`;
    label.htmlFor = select.id;
    spec.options.forEach((text, index) => {
      const option = node('option', '', text); option.value = index; select.append(option);
    });
    select.value = model.value;
    select.addEventListener('change', () => {
      const before = model.value; model.value = Number(select.value);
      history([{ key, before, after: model.value }]);
      status(`${spec.label}: ${spec.options[model.value]} · demo parameter`);
    });
    well.append(select); row.append(label, well); return row;
  }
  function toggle(spec, key) {
    const model = parameter(key, spec.value);
    const row = node('div', 'toggle-row'); row.dataset.slate = 'SMixtormatToggle';
    const label = node('span', '', spec.label);
    const button = node('button', 'toggle well');
    button.setAttribute('aria-label', spec.label);
    button.disabled = !!spec.disabled;
    button.setAttribute('aria-pressed', String(model.value));
    button.append(node('span', 'toggle-fill'));
    button.addEventListener('click', () => {
      const before = model.value; model.value = !model.value;
      button.setAttribute('aria-pressed', String(model.value));
      history([{ key, before, after: model.value }]);
    });
    row.append(label, button); return row;
  }
  function curve() {
    const container = node('div');
    container.append(node('div', 'state-label', 'Curve Bias · representative editor geometry'));
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 280 100'); svg.classList.add('curve');
    svg.setAttribute('role', 'img'); svg.setAttribute('aria-label', 'Representative scalar curve with grid and control points');
    const add = (tag, attrs) => {
      const shape = document.createElementNS('http://www.w3.org/2000/svg', tag);
      Object.entries(attrs).forEach(([name, value]) => shape.setAttribute(name, value)); svg.append(shape);
    };
    [20, 40, 60, 80].forEach(y => add('path', { d: `M0 ${y}H280`, class: 'grid-line' }));
    [35, 70, 105, 140, 175, 210, 245].forEach(x => add('path', { d: `M${x} 0V100`, class: 'grid-line' }));
    add('path', { d: 'M12 88 C72 88 81 65 140 50 S205 12 268 12', 'stroke-width': 1.5 });
    [[12, 88], [140, 50], [268, 12]].forEach(([cx, cy]) => add('circle', { cx, cy, r: 3 }));
    container.append(svg); return container;
  }
  function segments(spec, key) {
    const model = parameter(key, 0);
    const row = node('div', 'segmented'); row.dataset.slate = 'SMixtormatSegmentedControl';
    spec.options.forEach((text, index) => {
      const button = node('button', '', text);
      button.setAttribute('aria-pressed', String(model.value === index));
      button.addEventListener('click', () => {
        const before = model.value; model.value = index;
        history([{ key, before, after: index }]);
        row.querySelectorAll('button').forEach((item, i) => item.setAttribute('aria-pressed', String(i === index)));
      });
      row.append(button);
    });
    return row;
  }
  function debugButton(title) {
    const button = icon('debug', `Preview ${title}`, 'card-debug');
    button.classList.add('is-debug'); button.setAttribute('aria-pressed', 'false'); button.dataset.title = title;
    return button;
  }
  function eyeButton(title) {
    const button = icon('eye', `Toggle ${title} visibility`, 'eye');
    button.setAttribute('aria-pressed', 'true'); return button;
  }
  function build(spec, key) {
    if (spec.type === 'number') return dragger(spec, key);
    if (spec.type === 'dropdown') return dropdown(spec, key);
    if (spec.type === 'toggle') return toggle(spec, key);
    if (spec.type === 'curve') return curve();
    if (spec.type === 'segments') return segments(spec, key);
    if (spec.type === 'pair') {
      const row = node('div', 'pair'); spec.rows.forEach((item, index) => row.append(build(item, `${key}.${index}`))); return row;
    }
    if (spec.type === 'foldout') {
      const section = node('details', 'foldout'); section.open = spec.open;
      section.dataset.slate = 'SMixtormatInspectorGroup';
      const header = node('summary', '', spec.title.toUpperCase());
            // Own element so the accent multiply pass can composite separately from the additive lift.
            header.append(node('span', 'foldout-tint'));
      const body = node('div', 'foldout-body');
      spec.rows.forEach((row, index) => body.append(build(row, `${key}.${index}`)));
      section.append(header, body); return section;
    }
    if (spec.type === 'card') {
      const card = node('section', 'group-card'); card.dataset.slate = 'SMixtormatInspectorCard';
      const header = node('div', 'card-header');
      if (spec.eye === 'left') header.append(eyeButton(spec.title));
      header.append(node('span', 'card-title', spec.title));
      const actions = node('div', 'header-actions');
      if (spec.eye === 'right') actions.append(eyeButton(spec.title));
      if (spec.debug) actions.append(debugButton(spec.title));
      const reset = icon('reset', `Reset ${spec.title}`, 'reset-group');
      actions.append(reset); header.append(actions);
      const body = node('div', 'card-body');
      spec.rows.forEach((row, index) => body.append(build(row, `${key}.${index}`)));
      card.append(header, body); return card;
    }
    return node('span', 'state-label', 'Unsupported prototype control');
  }
  function renderInspector() {
    const schema = data.schemas[state.inspector];
    byId('selectionName').textContent = schema.title;
    byId('selectionKind').textContent = schema.kind;
    byId('selectionBadge').textContent = schema.badge;
    const container = byId('inspectorContent'); container.replaceChildren();
    schema.groups.forEach((group, index) => container.append(build(group, `${state.inspector}.${index}`)));
    if (state.inspector === 'states') {
      const driver = node('button', '', 'Open compact parameter-driver popover');
      driver.dataset.action = 'driver'; container.append(driver);
    }
  }
  function selectInspector(name) {
    if (!data.schemas[name]) return;
    state.inspector = name;
    document.querySelectorAll('.layer-row').forEach(row => row.classList.toggle('selected', row.dataset.inspector === name));
    renderInspector();
    status(`Inspecting ${data.schemas[name].title} · representative prototype controls`);
  }

  // --- Workspace / galleries ---------------------------------------------------------------
  function renderLayers() {
    const tree = byId('layerTree'); tree.replaceChildren();
    data.layers.forEach(layer => {
      const row = node('div', `layer-row${layer.child ? ' child' : ''}${layer.group ? ' group' : ''}`);
      row.setAttribute('role', 'listitem');
      if (layer.schema) row.dataset.inspector = layer.schema;
      row.append(eyeButton(layer.title));
      if (layer.thumb) row.append(node('div', `thumbnail ${layer.thumb}`));
      const select = node('button', 'layer-name', layer.title);
      if (layer.schema) select.dataset.inspector = layer.schema;
      else select.disabled = true;
      row.append(select);
      if (layer.source) row.append(node('span', 'layer-source badge', layer.source));
      tree.append(row);
    });
  }
  function renderGallery() {
    const search = byId('gallerySearch').value.toLowerCase();
    const category = byId('galleryCategory').value;
    const grid = byId('galleryGrid'); grid.replaceChildren();
    data[state.gallery].filter(([name, family]) => name.toLowerCase().includes(search) && (category === 'All' || family === category || state.gallery === 'masks')).forEach(([name, family, style]) => {
      const tile = node('button', 'gallery-tile'); tile.title = `${name} · ${family} · demo thumbnail`;
      tile.append(node('div', `swatch ${style}`), node('span', '', name));
      tile.addEventListener('click', () => {
        selectInspector(state.gallery === 'masks' ? 'mask' : 'surface');
        status(`${name} selected · gallery thumbnails are procedural placeholders`);
      });
      grid.append(tile);
    });
    if (!grid.children.length) grid.append(node('p', 'panel-footnote', 'No matching demo assets.'));
  }
  byId('gallerySearch').addEventListener('input', renderGallery);
  byId('galleryCategory').addEventListener('change', renderGallery);
  byId('meshSelect').addEventListener('change', event => { byId('viewport').dataset.mesh = event.target.value; });
  byId('lightSelect').addEventListener('change', event => { byId('viewport').dataset.light = event.target.value; });

  // --- Token editing: authored defaults remain in tokens.css -------------------------------
  const FALLOFF_NOTE = 'Below 1 fades earlier, 1 is linear, above 1 holds the top longer.';
  const groups = {
    'Typography': [
      ['body-size', 6, 24, 1, 'px'], ['dragger-font-size', 6, 24, 1, 'px'],
      ['caption-size', 6, 24, 1, 'px'], ['value-weight', 100, 900, 100],
      ['card-title-size', 7, 20, 1, 'px'], ['card-title-weight', 400, 700, 100],
      ['card-title-tracking', 0, 3, .1, 'px'],
      ['foldout-title-size', 6, 16, 1, 'px'], ['foldout-title-weight', 100, 900, 100], ['foldout-title-tracking', 0, 4, .1, 'px'],
      ['layer-group-title-size', 6, 18, 1, 'px'], ['layer-group-title-weight', 400, 700, 100]
    ],
    'Gradient saturation': [
      ...['foldout', 'foldout-hover', 'card-header', 'card-body', 'layer', 'layer-hover',
        'layer-selected', 'layer-group', 'child', 'child-hover', 'child-selected',
        'fill', 'fill-hover', 'fill-active', 'fill-disabled', 'well', 'well-hover']
        .map(role => [`${role}-saturation`, 0, 4, .1, '', '0 is grayscale; 1 preserves the source; above 1 increases saturation. Neutral colors have no saturation to increase.'])
    ],
    'Child layer gradients': [
      ...['child-left', 'child-right', 'child-hover-left', 'child-hover-right',
        'child-selected-left', 'child-selected-right'].map(role => [`${role}-opacity`, 0, 1, .01])
    ],
    'Compositing': [
      ['surface-lift-opacity', 0, 1, .01], ['hover-lift-opacity', 0, 1, .01],
      ['well-shade-top', 0, 1, .01], ['well-shade-bottom', 0, 1, .01],
      ['well-border-opacity', 0, 1, .01], ['well-border-top-opacity', 0, 1, .01], ['well-border-bottom-opacity', 0, 1, .01], ['well-border-hover-opacity', 0, 1, .01], ['well-border-hover-top-opacity', 0, 1, .01], ['well-border-hover-bottom-opacity', 0, 1, .01], ['well-border-saturation', 0, 4, .1],
      ['zero-tick-opacity', 0, 1, .01]
    ],
    'Slider fill': [
      ['fill-body-top', 0, 1, .01], ['fill-body-bottom', 0, 1, .01],
      ['fill-body-hover-top', 0, 1, .01], ['fill-body-hover-bottom', 0, 1, .01],
      ['fill-body-active-top', 0, 1, .01], ['fill-body-active-bottom', 0, 1, .01],
      ['fill-disabled-opacity', 0, 1, .01],
      ['fill-shade-start', 0, 1, .01], ['fill-shade-mid', 0, 1, .01], ['fill-shade-end', 0, 1, .01],
      ['fill-shade-mid-position', 0, 100, 1, '%'],
      ['fill-falloff-power', .05, 4, .05, '', FALLOFF_NOTE]
    ],
    'Group cards': [
      ['card-header-opacity', 0, 1, .01], ['card-body-opacity', 0, 1, .01],
      ['card-radius', 0, 12, 1, 'px'], ['card-header-height', 12, 64, 1, 'px'],
      ...['left', 'top', 'right', 'bottom'].map(side => [`card-header-${side}`, 0, 32, 1, 'px']),
      ...['left', 'top', 'right', 'bottom'].map(side => [`card-outer-${side}`, 0, 32, 1, 'px']),
      ['card-body-horizontal', 0, 32, 1, 'px'], ['card-body-top', 0, 32, 1, 'px'], ['card-body-bottom', 0, 32, 1, 'px'],
      ['card-header-margin-top', 0, 24, 1, 'px'], ['card-header-margin-bottom', 0, 24, 1, 'px'],
      ['card-title-opacity', 0, 1, .01],
      ['card-eye-size', 8, 24, 1, 'px'], ['card-leading-gap', 0, 16, 1, 'px'],
      ['card-falloff-power', .05, 4, .05, '', FALLOFF_NOTE], ['card-icon-size', 6, 32, 1, 'px'], ['card-icon-opacity', 0, 1, .01]
    ],
    'Foldouts': [['foldout-height', 12, 48, 1, 'px'], ['foldout-title-opacity', 0, 1, .01], ['foldout-gutter', 0, 20, 1, 'px'], ['foldout-outer-top', 0, 24, 1, 'px'], ['foldout-outer-bottom', 0, 24, 1, 'px'], ['foldout-header-padding-top', 0, 16, 1, 'px'], ['foldout-header-padding-bottom', 0, 16, 1, 'px'], ['foldout-body-top', 0, 24, 1, 'px'], ['foldout-body-bottom', 0, 24, 1, 'px'], ['header-tint-opacity', 0, 1, .01], ['header-hover-opacity', 0, 1, .01], ['foldout-falloff-power', .05, 4, .05, '', FALLOFF_NOTE], ['foldout-accent-multiply-opacity', 0, 1, .01], ['foldout-accent-hover-multiply-opacity', 0, 1, .01], ['foldout-hairline-opacity', 0, 1, .01], ['hairline-hover-opacity', 0, 1, .01], ['foldout-icon-size', 6, 32, 1, 'px'], ['foldout-icon-opacity', 0, 1, .01]],
        'Layer rows': [['layer-height', 18, 48, 1, 'px'], ['child-height', 14, 40, 1, 'px'], ['layer-group-height', 14, 40, 1, 'px'], ['layer-badge-width', 32, 80, 1, 'px'], ['layer-icon-size', 6, 32, 1, 'px'], ['layer-icon-opacity', 0, 1, .01], ['toolbar-icon-size', 6, 32, 1, 'px'], ['toolbar-icon-opacity', 0, 1, .01]],
    'Layer and overlay states': [['group-cross-opacity', 0, 1, .01], ['overlay-ground-opacity', 0, 1, .01], ['overlay-plate-opacity', 0, 1, .01], ['overlay-hover-accent', 0, 1, .01], ['overlay-press-accent', 0, 1, .01], ['overlay-icon-rest-opacity', 0, 1, .01], ['overlay-icon-size', 6, 32, 1, 'px'], ['overlay-icon-opacity', 0, 1, .01], ['overlay-grip-opacity', 0, 1, .01]],
    'Context menus and hover help': [['popup-lip-height', 10, 50, 1, 'px'], ['popup-tint-opacity', 0, 1, .01], ['popup-border-opacity', 0, 1, .01], ['popup-shadow-opacity', 0, 1, .01], ['menu-icon-size', 6, 32, 1, 'px'], ['menu-icon-opacity', 0, 1, .01], ['menu-width', 150, 360, 5, 'px'], ['menu-row-height', 18, 32, 1, 'px'], ['menu-padding', 0, 12, 1, 'px'], ['help-max-width', 200, 500, 5, 'px'], ['help-padding', 4, 20, 1, 'px'], ['help-delay', 150, 1000, 50, 'ms']],
    'Controls': [['row-height', 16, 36, 1, 'px'], ['row-gap', 0, 12, 1, 'px'], ['paired-gap', 0, 12, 1, 'px'], ['dragger-text-inset', 2, 20, 1, 'px'], ['dropdown-label-ratio', .15, .6, .01], ['control-label-opacity', 0, 1, .01], ['control-value-opacity', 0, 1, .01], ['well-border-width', 0, 3, .5, 'px'], ['well-radius', 0, 12, 1, 'px'], ['modified-stripe-width', 0, 8, .5, 'px'], ['modified-stripe-opacity', 0, 1, .01], ['icon-size', 8, 32, 1, 'px'], ['icon-hit-padding', 0, 8, .5, 'px'], ['icon-off-opacity', 0, 1, .01], ['layer-module-icon-size', 8, 32, 1, 'px'], ['layer-module-icon-opacity', 0, 1, .01], ['badge-width', 48, 96, 1, 'px'], ['thumbnail-size', 12, 64, 1, 'px'], ['thumbnail-radius', 0, 12, 1, 'px'], ['gallery-swatch-radius', 0, 12, 1, 'px'], ['icon-sheet-preview-size', 8, 48, 1, 'px'], ['rail-icon-size', 12, 48, 1, 'px'], ['window-grip-size', 6, 32, 1, 'px'], ['window-grip-opacity', 0, 1, .01]],
    'Workspace': [['left-width', 160, 640, 5, 'px'], ['inspector-width', 200, 720, 5, 'px'], ['gallery-height', 80, 500, 5, 'px'], ['gallery-tile-size', 50, 140, 2, 'px'], ['splitter-size', 1, 5, 1, 'px'], ['splitter-hit-size', 5, 12, 1, 'px'], ['topbar-icon-size', 6, 32, 1, 'px'], ['topbar-icon-opacity', 0, 1, .01]]
  };
  const tokenDefaults = new Map();
  const tokenInputs = new Map();
  const computed = getComputedStyle(root);
  function renderTokens() {
    const controls = byId('tokenControls');
    const sections = [];
    const addSection = (title, build) => {
      const section = node('section', 'token-section');
      section.append(node('div', 'token-category', title));
      section.append(build());
      sections.push({ title, element: section });
      controls.append(section);
    };
    Object.entries(groups).forEach(([category, entries]) => {
      addSection(category.toUpperCase(), () => {
      const section = node('div', 'token-grid');
      entries.forEach(([name, min, max, step, unit = '', note = '']) => {
        const cssName = `--${name}`;
        const authored = computed.getPropertyValue(cssName).trim();
        tokenDefaults.set(cssName, authored);
        const row = node('div', 'token-row'); row.dataset.tokenName = name; row.dataset.cssToken = cssName;
        row.dataset.help = `${category} · ${name}\nRange: ${min}–${max}${unit}. Default: ${authored}.${note ? ` ${note}` : ''} Right-click for token actions.`;
        const label = node('label', '', name); label.htmlFor = `token-${name}`;
        const input = node('input'); input.id = label.htmlFor; input.type = 'number';
        input.min = min; input.max = max; input.step = step; input.value = parseFloat(authored);
        input.addEventListener('input', () => {
          const value = input.valueAsNumber;
          if (!Number.isFinite(value) || value < min || value > max) return;
          root.style.setProperty(cssName, `${value}${unit}`);
          window.MixtormatPrototype.refreshFalloff();
        });
        const reset = node('button', '', '↺'); reset.setAttribute('aria-label', `Reset ${name}`);
        reset.addEventListener('click', () => { root.style.removeProperty(cssName); input.value = parseFloat(authored); window.MixtormatPrototype.refreshFalloff(); });
        tokenInputs.set(cssName, input); row.append(label, input, reset); section.append(row);
      });
      return section;
      });
    });
    {
      const section = sections.find(section => section.title === 'TYPOGRAPHY').element.querySelector('.token-grid');
      const cssName = '--font-family';
      const authored = computed.getPropertyValue(cssName).trim();
      tokenDefaults.set(cssName, authored);
      const row = node('div', 'token-row');
      row.dataset.tokenName = 'font-family'; row.dataset.cssToken = cssName;
      row.dataset.help = 'UI font family. Roboto and Inter are bundled locally. Right-click for token actions.';
      const label = node('label', '', 'font-family'); label.htmlFor = 'token-font-family';
      const select = node('select'); select.id = label.htmlFor;
      const fonts = new Map([[authored, 'Default font'], ["'Roboto', sans-serif", 'Roboto'], ["'Inter', sans-serif", 'Inter']]);
      fonts.forEach((title, value) => {
        const option = node('option', '', title); option.value = value; select.append(option);
      });
      select.value = authored;
      select.addEventListener('change', () => root.style.setProperty(cssName, select.value));
      const reset = node('button', '', '↺'); reset.setAttribute('aria-label', 'Reset font family');
      reset.addEventListener('click', () => resetToken(cssName));
      tokenInputs.set(cssName, select);
      row.append(label, select, reset); section.prepend(row);
    }
    addSection('BASE PALETTE', () => {
    const section = node('div', 'token-grid');
    ['ground', 'text', 'accent', 'modified', 'child-left', 'child-right', 'child-hover-left', 'child-hover-right', 'child-selected-left', 'child-selected-right'].forEach(name => {
      const cssName = `--${name}-rgb`;
      const channels = computed.getPropertyValue(cssName).trim(); tokenDefaults.set(cssName, channels);
      const toHex = value => `#${value.split(/\s+/).map(channel => Number(channel).toString(16).padStart(2, '0')).join('')}`;
      const row = node('div', 'token-row'); row.dataset.tokenName = name; row.dataset.cssToken = cssName;
      row.dataset.help = `${name} RGB source. Opacity and additive/multiply layers are controlled separately. Right-click to reset this color.`;
      const label = node('label', '', `${name}-rgb`); label.htmlFor = `token-${name}`;
      const input = node('input'); input.type = 'color'; input.id = label.htmlFor; input.value = toHex(channels);
      input.addEventListener('input', () => root.style.setProperty(cssName, [1, 3, 5].map(index => parseInt(input.value.slice(index, index + 2), 16)).join(' ')));
      const reset = node('button', '', '↺'); reset.setAttribute('aria-label', `Reset ${name}`);
      reset.addEventListener('click', () => { root.style.removeProperty(cssName); input.value = toHex(channels); });
      tokenInputs.set(cssName, input); row.append(label, input, reset); section.append(row);
    });
    return section;
    });
    addSection('BLEND OPERATIONS', () => {
    const section = node('div', 'token-grid');
    ['surface-blend-mode', 'well-blend-mode', 'foldout-blend-mode', 'foldout-accent-blend-mode', 'card-blend-mode', 'group-button-blend-mode', 'layer-blend-mode', 'layer-group-blend-mode'].forEach(name => {
      const cssName = `--${name}`; const authored = computed.getPropertyValue(cssName).trim(); tokenDefaults.set(cssName, authored);
      const row = node('div', 'token-row'); row.dataset.tokenName = name; row.dataset.cssToken = cssName;
      row.dataset.help = `${name}: plus-lighter adds the source color; multiply darkens it; screen lifts it; normal uses source-over opacity.`;
      const label = node('label', '', name); const select = node('select');
      select.id = `token-${name}`; label.htmlFor = select.id;
      ['normal', 'plus-lighter', 'multiply', 'screen', 'overlay', 'soft-light', 'hard-light', 'color-dodge', 'color-burn', 'darken', 'lighten', 'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity'].forEach(mode => select.append(node('option', '', mode)));
      select.value = authored; select.addEventListener('change', () => root.style.setProperty(cssName, select.value));
      tokenInputs.set(cssName, select); row.append(label, select); section.append(row);
    });
    return section;
    });
    // Tab strip: one button per category, arrow-key navigable like the other tab lists.
    const tabs = node('div', 'token-tabs'); tabs.setAttribute('role', 'tablist'); tabs.setAttribute('aria-label', 'Token categories');
    sections.forEach((section, index) => {
      const button = node('button', '', section.title);
      button.type = 'button'; button.setAttribute('role', 'tab');
      button.addEventListener('click', () => selectTokenTab(index));
      tabs.append(button);
    });
    tabs.addEventListener('keydown', event => {
      const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
      if (!step) return;
      event.preventDefault();
      const current = sections.findIndex(section => !section.element.hidden);
      const next = (current + step + sections.length) % sections.length;
      selectTokenTab(next); tabs.children[next].focus();
    });
    controls.prepend(tabs);
    selectTokenTab(0);
  }

  // One category at a time: the panel is for tweaking, and a tab strip keeps every token
  // reachable without a long scroll through the ones you are not working on.
  function selectTokenTab(index) {
    const controls = byId('tokenControls');
    const tabs = controls.querySelector('.token-tabs');
    const sections = [...controls.querySelectorAll('.token-section')];
    tabs?.querySelectorAll('button').forEach((button, i) => {
      button.setAttribute('aria-selected', String(i === index));
      button.tabIndex = i === index ? 0 : -1;
    });
    sections.forEach((section, i) => { section.hidden = i !== index; });
  }

  byId('tokenSearch').addEventListener('input', event => {
    const search = event.target.value.toLowerCase();
    const controls = byId('tokenControls');
    // A search spans every category, so the tab strip steps aside while one is active.
    document.querySelectorAll('.token-row').forEach(row => { row.hidden = !row.dataset.tokenName.includes(search); });
    controls.classList.toggle('searching', Boolean(search));
    controls.querySelectorAll('.token-section').forEach(section => { section.hidden = false; });
  });
  function exportTokens() {
    const current = getComputedStyle(root);
    return Object.fromEntries([...tokenDefaults.keys()].map(name => [name, current.getPropertyValue(name).trim()]));
  }
  function resetToken(name) {
    const authored = tokenDefaults.get(name);
    if (authored === undefined) return;
    root.style.removeProperty(name);
        window.MixtormatPrototype.refreshFalloff();
    const input = tokenInputs.get(name);
    if (input?.type === 'number') input.value = parseFloat(authored);
    else if (input?.type === 'color') input.value = `#${authored.split(/\s+/).map(channel => Number(channel).toString(16).padStart(2, '0')).join('')}`;
    else if (input) input.value = authored;
  }
  function resetTokens() { tokenDefaults.forEach((_, name) => resetToken(name)); }
  function download(name, value) {
    const url = URL.createObjectURL(new Blob([JSON.stringify(value, null, 2)], { type: 'application/json' }));
    const anchor = node('a'); anchor.href = url; anchor.download = name; anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  // --- Dialogs / parameter popover ---------------------------------------------------------
  function modal(title, paragraphs, extras) {
    const content = byId('modalContent'); content.replaceChildren(node('h2', '', title));
    paragraphs.forEach(text => content.append(node('p', '', text)));
    if (extras) content.append(extras);
    byId('modal').showModal();
  }
  function driver(anchor) {
    const popover = byId('popover'); popover.replaceChildren();
    const body = node('div', 'compact-card'); body.dataset.slate = 'SMixtormatDriverPopover / CompactLayout';
    body.append(node('h3', '', 'PARAMETER DRIVER · PREVIEW ONLY'), dropdown({ label: 'Source', options: ['None', 'Layer Output', 'Expression'], selected: 0 }, 'driver.source'), dragger({ label: 'Multiplier', value: 1, min: 0, max: 4 }, 'driver.multiplier'), dragger({ label: 'Offset', value: 0, min: -1, max: 1 }, 'driver.offset'), node('span', 'state-label', 'Compact geometry is independent of inspector cards.'));
    popover.append(body); popover.hidden = false;
    const rect = anchor.getBoundingClientRect();
    popover.style.left = `${Math.max(8, Math.min(rect.left - 200, window.innerWidth - 260))}px`;
    popover.style.top = `${Math.max(8, Math.min(rect.bottom + 4, window.innerHeight - 210))}px`;
  }
  document.addEventListener('pointerdown', event => {
    if (!byId('popover').contains(event.target) && !event.target.closest('[data-action=driver]')) byId('popover').hidden = true;
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') { byId('popover').hidden = true; byId('styleDrawer').hidden = true; }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z' && !['INPUT', 'SELECT'].includes(event.target.tagName)) {
      event.preventDefault(); applyHistory(event.shiftKey ? 'redo' : 'undo');
    }
  });

  // --- Central action routing --------------------------------------------------------------
  document.addEventListener('click', event => {
    const button = event.target.closest('button');
    if (!button || button.disabled) return;
    if (button.dataset.inspector) return selectInspector(button.dataset.inspector);
    if (button.dataset.leftTab) {
      document.querySelectorAll('[data-left-tab]').forEach(item => item.setAttribute('aria-selected', String(item === button)));
      byId('layersPanel').hidden = button.dataset.leftTab !== 'layers';
      byId('libraryPanel').hidden = button.dataset.leftTab !== 'library'; return;
    }
    if (button.dataset.gallery) {
      state.gallery = button.dataset.gallery;
      document.querySelectorAll('[data-gallery]').forEach(item => item.setAttribute('aria-selected', String(item === button)));
      renderGallery(); return;
    }
    if (button.dataset.channel) {
      byId('viewport').dataset.channel = button.dataset.channel;
      document.querySelectorAll('[data-channel]').forEach(item => { if (item.tagName === 'BUTTON') item.setAttribute('aria-pressed', String(item === button)); }); return;
    }
    switch (button.dataset.action) {
      case 'undo': case 'redo': applyHistory(button.dataset.action); break;
      case 'style': byId('styleDrawer').hidden = !byId('styleDrawer').hidden; break;
      case 'close-style': byId('styleDrawer').hidden = true; break;
      case 'reset-style': resetTokens(); status('Prototype style defaults restored'); break;
      case 'export-tokens': download('mixtormat-prototype-tokens.json', { version: 1, source: 'HTML visual contract', tokens: exportTokens() }); break;
      case 'states': selectInspector('states'); break;
      case 'collapse': document.querySelectorAll('#inspectorContent details').forEach(section => { section.open = false; }); break;
      case 'eye': {
        const visible = button.getAttribute('aria-pressed') !== 'true';
        button.setAttribute('aria-pressed', String(visible));
        const image = button.querySelector('img');
        if (image) image.src = `icons/${visible ? 'eye' : 'eye-off'}.png`;
        button.closest('.layer-row')?.classList.toggle('is-hidden', !visible);
        status(`${button.getAttribute('aria-label')}: ${visible ? 'on' : 'off'} · mock visibility`); break;
      }
      case 'card-debug': case 'debug': {
        const active = button.getAttribute('aria-pressed') !== 'true';
        document.querySelectorAll('[data-action=card-debug], [data-action=debug]').forEach(item => {
          item.setAttribute('aria-pressed', 'false');
          const image = item.querySelector('img');
          if (image) image.src = 'icons/eye-off.png';
        });
        button.setAttribute('aria-pressed', String(active));
        const image = button.querySelector('img');
        if (image) image.src = `icons/${active ? 'eye' : 'eye-off'}.png`;
        byId('debugLabel').textContent = active ? `Debug: ${button.dataset.title || 'Output'}` : 'Composite';
        status('Debug selection updated · visual indication only, no shader evaluation'); break;
      }
      case 'reset-group': {
        const changes = [];
        const card = button.closest('.group-card');
        card.querySelectorAll('[data-parameter]').forEach(row => {
          const key = row.dataset.parameter; const model = state.parameters.get(key);
          changes.push({ key, before: model.value, after: model.defaultValue }); model.value = model.defaultValue;
        });
        history(changes); renderInspector(); status('Numeric controls in this group reset'); break;
      }
      case 'driver': driver(button); break;
      case 'wire': button.setAttribute('aria-pressed', String(byId('viewport').classList.toggle('wire'))); break;
      case 'frame': status('Preview framed · CSS illustration has a fixed camera'); break;
      case 'gallery': {
        const collapsed = document.querySelector('.center-panel').classList.toggle('gallery-collapsed');
        byId('galleryGrid').hidden = collapsed; button.textContent = collapsed ? 'Expand' : 'Collapse'; break;
      }
      case 'add-layer': data.layers.push({ title: 'Demo layer', schema: 'surface', source: 'DEMO', thumb: 'metal' }); renderLayers(); status('Added a local demo layer; no asset changed'); break;
      case 'save': state.dirty = false; syncHistory(); status('Demo saved in memory only. SAVE AS exports the prototype state.'); break;
      case 'export': download('mixtormat-prototype-state.json', { version: 1, simulated: true, parameters: Object.fromEntries([...state.parameters].map(([key, value]) => [key, value.value])), tokens: exportTokens() }); break;
      case 'new': modal('New workspace', ['This prototype does not create Unreal assets. A new workspace would confirm unsaved changes, then start an empty recipe. The sample remains available for comparing components.']); break;
      case 'load': modal('Load workspace', ['Unreal asset loading is intentionally not emulated. Use the layer selections to explore the included demo inspector schemas. SAVE AS exports the visual-contract state as JSON.']); break;
      case 'bake': {
        const rows = node('div', 'dialog-rows');
        rows.append(dropdown({ label: 'Resolution', options: ['1024', '2048', '4096'], selected: 1 }, 'bake.resolution'), toggle({ label: 'Base Color', value: true }, 'bake.bc'), toggle({ label: 'Normal', value: true }, 'bake.normal'), toggle({ label: 'RAM', value: true }, 'bake.ram'));
        const run = node('button', '', 'Bake unavailable in visual prototype'); run.disabled = true; rows.append(run);
        modal('Bake outputs', ['Layout and control states only. No textures are written and no GPU bake runs.'], rows); break;
      }
      case 'contract': modal('Unreal translation contract', [
        'Visual prototype, not a functional material editor. Schemas are representative, not an exhaustive list of engine parameters. Demo values are not promised engine defaults.',
        'Foldout → SMixtormatInspectorGroup. Non-collapsible card → SMixtormatInspectorCard. Numeric well → SMixtormatSlider. Dropdown → MixtormatRow::MakeDropdown + SMixtormatChip. Toggle → SMixtormatToggle. Segments → SMixtormatSegmentedControl.',
        'Palette channels and opacity belong in MixtormatPalette/LiveTheme. Geometry and typography belong in DesignTokens. Gradients and additive/multiply color math map to the shared Slate painter, not embedded HTML.',
        'Preserve current Unreal parameter bindings, preview actions, history, reset behavior, conditional visibility, and compact popover geometry. Browser compositing is a reference; Slate requires explicit paint/color math and in-engine comparison.',
        'Files: tokens.css (authored values), components.css (component rules), inspector-data.js (representative hierarchy), app.js (demo interactions). No external dependencies or storage.'
      ]); break;
    }
  });
  window.MixtormatPrototype = { node, icon, dragger, dropdown, toggle, segments, status, parameter, renderInspector, resetToken, resetTokens, driver };
  renderLayers(); renderInspector(); renderGallery(); renderTokens(); selectInspector('surface');
})();

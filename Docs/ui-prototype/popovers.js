/* Styled right-click menus and delayed hover/focus help.
   Actions delegate to existing prototype controls; no duplicate parameter state. */
'use strict';
(() => {
  const ui = window.MixtormatPrototype;
  const root = document.documentElement;
  const menu = ui.node('div', 'floating-surface context-menu');
  menu.hidden = true; menu.id = 'prototype-choice-menu'; menu.setAttribute('role', 'menu');
  const help = ui.node('div', 'floating-surface help-popover');
  help.hidden = true; help.id = 'prototype-hover-help'; help.setAttribute('role', 'tooltip');
  document.body.append(menu, help);
  let menuAnchor = null;
  let helpAnchor = null;
  let helpTimer = null;
  let previousDescription = null;

  const actionHelp = {
    style: 'UI Style\nEdit the visual contract live. Drag its title and resize its bottom-right grip.',
    'close-style': 'Close UI Style\nYour in-memory token edits are retained.',
    'reset-style': 'Reset style\nRestore prototype token defaults. No file or Unreal asset is changed.',
    'export-tokens': 'Export tokens\nDownload readable JSON for reviewing the Slate translation.',
    states: 'Component states\nInspect paired rows, signed draggers, disabled controls, and nested cards.',
    collapse: 'Collapse inspector foldouts\nNon-collapsible Group Cards are unaffected.',
    bake: 'Bake outputs\nPrototype layout only. Actual texture baking requires Unreal.',
    undo: 'Undo\nUndo the last demo parameter edit.',
    redo: 'Redo\nReapply the last undone demo parameter edit.',
    contract: 'Translation contract\nRead component mappings, scope, and integration boundaries.'
  };
  function migrateTitles(scope) {
    const elements = [...scope.querySelectorAll('[title]')];
    if (scope.matches?.('[title]')) elements.unshift(scope);
    elements.forEach(element => {
      if (!element.dataset.help) element.dataset.help = element.title;
      element.removeAttribute('title');
    });
    scope.querySelectorAll('[data-action]').forEach(element => {
      const description = actionHelp[element.dataset.action];
      if (description && !element.dataset.help) element.dataset.help = description;
    });
  }
  migrateTitles(document);
  new MutationObserver(records => records.forEach(record => record.addedNodes.forEach(element => {
    if (element.nodeType === Node.ELEMENT_NODE) migrateTitles(element);
  }))).observe(document.body, { childList: true, subtree: true });

  function surfaceHost(anchor) {
    // A modal dialog lives in the top layer; its popovers must live inside it too.
    return anchor.closest('dialog[open]') || document.body;
  }
  function position(surface, x, y) {
    const box = surface.getBoundingClientRect();
    surface.style.left = `${Math.max(8, Math.min(x, window.innerWidth - box.width - 8))}px`;
    surface.style.top = `${Math.max(8, Math.min(y, window.innerHeight - box.height - 8))}px`;
  }
  function hideHelp() {
    clearTimeout(helpTimer); helpTimer = null; help.hidden = true;
    if (helpAnchor) {
      if (previousDescription === null) helpAnchor.removeAttribute('aria-describedby');
      else helpAnchor.setAttribute('aria-describedby', previousDescription);
    }
    helpAnchor = null; previousDescription = null;
  }
  function scheduleHelp(anchor, immediate = false) {
    if (!menu.hidden || !anchor || anchor === helpAnchor) return;
    hideHelp();
    helpAnchor = anchor;
    previousDescription = anchor.getAttribute('aria-describedby');
    const delay = immediate ? 0 : parseFloat(getComputedStyle(root).getPropertyValue('--help-delay'));
    helpTimer = setTimeout(() => {
      if (!anchor.isConnected || anchor.closest('[hidden]')) { hideHelp(); return; }
      const lines = anchor.dataset.help.split('\n');
      help.replaceChildren();
      if (lines.length > 1) {
        help.append(ui.node('strong', 'help-title', lines.shift()), ui.node('span', 'help-body', lines.join('\n')));
      } else help.append(ui.node('span', 'help-body', lines[0]));
      surfaceHost(anchor).append(help); help.hidden = false;
      anchor.setAttribute('aria-describedby', `${previousDescription || ''} ${help.id}`.trim());
      const box = anchor.getBoundingClientRect();
      position(help, box.left, box.bottom + 7);
    }, Number.isFinite(delay) ? delay : 350);
  }
  document.addEventListener('pointerover', event => {
    const native = event.target.closest('[title]');
    if (native) migrateTitles(native);
    scheduleHelp(event.target.closest('[data-help]'));
  });
  document.addEventListener('pointerout', event => {
    if (helpAnchor && !helpAnchor.contains(event.relatedTarget)) hideHelp();
  });
  document.addEventListener('focusin', event => scheduleHelp(event.target.closest('[data-help]'), true));
  document.addEventListener('focusout', event => {
    if (helpAnchor && !helpAnchor.contains(event.relatedTarget)) hideHelp();
  });

  function closeMenu(restoreFocus = false) {
    menu.hidden = true;
    menuAnchor?.setAttribute('aria-expanded', 'false');
    if (restoreFocus && menuAnchor?.isConnected) menuAnchor.focus?.();
    menuAnchor = null;
  }
  function action(label, run, options = {}) {
    const item = ui.node('button', 'menu-item');
    item.type = 'button'; item.setAttribute('role', 'menuitem');
    item.disabled = !!options.disabled;
    if (options.icon) {
      const image = ui.node('img', 'asset-icon'); image.src = `icons/${options.icon}.png`; image.alt = ''; item.append(image);
    }
    item.append(ui.node('span', '', label));
    if (options.shortcut) item.append(ui.node('span', 'menu-shortcut', options.shortcut));
    item.addEventListener('click', () => { closeMenu(); run(); });
    menu.append(item); return item;
  }

  const dropdowns = new Map();
  function styleDropdown(select) {
    if (dropdowns.has(select)) return;
    const button = ui.node('button', 'dropdown-trigger'); button.type = 'button';
    button.id = `${select.id || `dropdown-${dropdowns.size}`}-trigger`;
    const labels = [...select.labels];
    labels.forEach(label => { label.htmlFor = button.id; });
    button.setAttribute('aria-label', labels.map(label => label.textContent).join(' ') || select.getAttribute('aria-label') || 'Select option');
    button.setAttribute('aria-haspopup', 'menu'); button.setAttribute('aria-controls', menu.id); button.setAttribute('aria-expanded', 'false');
    const text = ui.node('span', 'dropdown-selection');
    const arrow = ui.node('img', 'asset-icon'); arrow.src = 'icons/chevron-down.png'; arrow.alt = '';
    button.append(text, arrow);
    const sync = () => { text.textContent = select.selectedOptions[0]?.textContent || ''; button.disabled = select.disabled; };
    dropdowns.set(select, { button, sync });
    select.classList.add('custom-dropdown-source'); select.hidden = true; select.before(button);
    select.addEventListener('change', sync);
    const open = () => {
      if (select.disabled) return;
      if (!menu.hidden && menuAnchor === button) { closeMenu(true); return; }
      hideHelp(); closeMenu(); menuAnchor = button;
      surfaceHost(button).append(menu); menu.replaceChildren(); menu.setAttribute('aria-label', button.getAttribute('aria-label'));
      [...select.options].forEach(option => {
        const item = action(option.textContent, () => {
          select.value = option.value; select.dispatchEvent(new Event('change', { bubbles: true })); sync(); button.focus();
        }, { disabled: option.disabled || !!option.closest('optgroup:disabled'), icon: option.selected ? 'check' : undefined });
        if (!option.selected) item.prepend(ui.node('span', 'asset-icon'));
        item.setAttribute('role', 'menuitemradio'); item.setAttribute('aria-checked', String(option.selected));
      });
      menu.hidden = false; button.setAttribute('aria-expanded', 'true');
      const box = button.getBoundingClientRect(); position(menu, box.left, box.bottom + 2);
      (menu.querySelector('[aria-checked=true]:not(:disabled)') || menu.querySelector('button:not(:disabled)'))?.focus();
    };
    button.addEventListener('click', open);
    button.addEventListener('keydown', event => {
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); open(); }
    });
    sync();
  }
  function styleDropdowns(scope) {
    if (scope.matches?.('select')) styleDropdown(scope);
    scope.querySelectorAll?.('select').forEach(styleDropdown);
  }
  styleDropdowns(document);
  new MutationObserver(records => records.forEach(record => record.addedNodes.forEach(element => {
    if (element.nodeType === Node.ELEMENT_NODE) styleDropdowns(element);
  }))).observe(document.body, { childList: true, subtree: true });
  new MutationObserver(() => {
    dropdowns.forEach(({ sync }, select) => {
      if (select.isConnected) sync(); else dropdowns.delete(select);
    });
  }).observe(root, { attributes: true, attributeFilter: ['style'] });
  const trigger = selector => document.querySelector(selector)?.click();
  const separator = () => menu.append(ui.node('div', 'menu-separator'));
  document.addEventListener('contextmenu', event => {
    const target = event.target;
    if (!(target instanceof Element)) return;
    event.preventDefault(); hideHelp(); closeMenu();
    menuAnchor = target.closest('button, input, select, [tabindex]') || target;
    surfaceHost(target).append(menu); menu.replaceChildren();
    const token = target.closest('.token-row');
    const parameter = target.closest('.dragger');
    const card = target.closest('.group-card');
    const layer = target.closest('.layer-row');
    const foldout = target.closest('.foldout');
    const style = target.closest('#styleDrawer');
    const caption = token ? token.dataset.cssToken : parameter ? parameter.getAttribute('aria-label') : layer ? layer.querySelector('.layer-name').textContent : card ? card.querySelector('.card-title')?.textContent || card.getAttribute('aria-label') : foldout ? foldout.querySelector('summary').textContent : style ? 'UI Style' : 'Mixtormat';
    menu.setAttribute('aria-label', `${caption} context actions`);
    menu.append(ui.node('div', 'menu-caption', caption));
    if (token) {
      action('Reset this token', () => { ui.resetToken(token.dataset.cssToken); ui.status(`${token.dataset.cssToken} restored`); }, { icon: 'refresh' });
      action('Edit value', () => token.querySelector('input, .dropdown-trigger, select')?.focus());
      separator();
    } else if (parameter) {
      const disabled = parameter.getAttribute('aria-disabled') === 'true';
      action('Reset parameter', () => parameter.dispatchEvent(new CustomEvent('prototype:reset')), { icon: 'refresh', shortcut: 'Backspace', disabled });
      action('Parameter driver…', () => ui.driver(parameter), { icon: 'nodes', disabled });
      separator();
    }
    if (card) action('Reset group values', () => ui.resetGroup(card), { icon: 'refresh', disabled: !card.querySelector('[data-parameter]') });
    if (foldout) action('Reset foldout values', () => ui.resetGroup(foldout), { icon: 'refresh', disabled: !foldout.querySelector('[data-parameter]') });
    if (layer) {
      action('Inspect', () => layer.querySelector('.layer-name')?.click(), { disabled: !!layer.querySelector('.layer-name')?.disabled });
      action('Toggle visibility', () => layer.querySelector('[data-action=eye]')?.click(), { icon: 'eye' });
    }
    if (foldout) action(foldout.open ? 'Collapse foldout' : 'Expand foldout', () => { foldout.open = !foldout.open; }, { icon: foldout.open ? 'chevron-up' : 'chevron-down' });
    if (style) {
      action('Reset all style tokens', () => trigger('[data-action=reset-style]'), { icon: 'refresh' });
      action('Export tokens…', () => trigger('[data-action=export-tokens]'), { icon: 'save-as' });
      action('Close UI Style', () => trigger('[data-action=close-style]'));
    } else {
      action('UI Style…', () => { document.getElementById('styleDrawer').hidden = false; }, { icon: 'settings' });
      action('Component states', () => trigger('[data-action=states]'));
      action('Translation contract', () => trigger('[data-action=contract]'), { icon: 'documentation' });
    }
    menu.hidden = false;
    const box = target.getBoundingClientRect();
    position(menu, event.clientX || box.left, event.clientY || box.bottom);
    menu.querySelector('button:not(:disabled)')?.focus();
  });
  let menuSearch = '';
  let menuSearchTime = 0;
  menu.addEventListener('keydown', event => {
    const items = [...menu.querySelectorAll('button:not(:disabled)')];
    const index = items.indexOf(document.activeElement);
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      items[(index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length]?.focus();
    }
    if (event.key === 'Home' || event.key === 'End') {
      event.preventDefault(); items[event.key === 'Home' ? 0 : items.length - 1]?.focus();
    }
    if (event.key.length === 1 && event.key !== ' ' && !event.ctrlKey && !event.metaKey && !event.altKey) {
      event.preventDefault();
      const now = Date.now();
      menuSearch = now - menuSearchTime > 700 ? event.key.toLowerCase() : menuSearch + event.key.toLowerCase();
      menuSearchTime = now;
      items.find(item => item.textContent.trim().toLowerCase().startsWith(menuSearch))?.focus();
    }
    if (event.key === 'Tab') closeMenu();
  });
  document.addEventListener('keydown', event => {
    if (event.key !== 'Escape') return;
    hideHelp();
    if (!menu.hidden) {
      event.preventDefault(); event.stopImmediatePropagation(); closeMenu(true);
    }
  }, true);
  document.addEventListener('pointerdown', event => {
    hideHelp(); if (!menu.contains(event.target)) closeMenu();
  });
  document.addEventListener('scroll', event => {
    // Scrolling the menu itself must not dismiss it.
    if (!menu.contains(event.target)) { hideHelp(); closeMenu(); }
  }, true);
  window.addEventListener('resize', () => { hideHelp(); closeMenu(); });
})();

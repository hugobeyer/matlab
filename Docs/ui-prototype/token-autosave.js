/* Explicitly authorized JSON persistence. CSS remains the authored reset defaults.
   File handles are session-only: reconnect after reload. No server or storage fallback. */
'use strict';
(() => {
  const ui = window.MixtormatPrototype;
  const button = document.getElementById('connectTokenAutosave');
  const label = document.getElementById('tokenAutosaveStatus');
  let handle = null;
  let documentData = null;
  let timer = null;
  let saving = false;
  let changed = false;
  let snapshot = '';
  const current = () => JSON.stringify(ui.exportTokens());
  const status = text => { label.textContent = text; };

  async function save() {
    clearTimeout(timer);
    if (!handle || saving || !changed) return;
    saving = true; button.disabled = true;
    try {
      // Serialize writes; changes made during a write get another pass afterwards.
      do {
        changed = false;
        const tokens = ui.exportTokens();
        const stream = await handle.createWritable();
        await stream.write(JSON.stringify({ ...documentData, tokens: { ...documentData.tokens, ...tokens } }, null, 2));
        await stream.close();
      } while (changed);
      status(`Saved · ${handle.name}`);
    } catch (error) {
      handle = null;
      button.textContent = 'Connect autosave';
      status(`Autosave stopped: ${error.message}`);
    } finally {
      saving = false; button.disabled = false;
    }
  }

  if (!window.isSecureContext || typeof window.showSaveFilePicker !== 'function') {
    button.disabled = true;
    status('File autosave unavailable in this browser/context. Export tokens remains available.');
    return;
  }

  button.addEventListener('click', async () => {
    if (handle) {
      await save();
      if (!handle) return;
      handle = null; clearTimeout(timer);
      button.textContent = 'Connect autosave'; status('Disconnected · reconnect after reload');
      return;
    }
    button.disabled = true;
    try {
      const selected = await window.showSaveFilePicker({
        suggestedName: 'mixtormat-prototype-tokens.json',
        types: [{ description: 'Mixtormat token JSON', accept: { 'application/json': ['.json'] } }]
      });
      const text = await (await selected.getFile()).text();
      const data = text.trim() ? JSON.parse(text) : { version: 1, source: 'HTML visual contract', tokens: {} };
      if (data.version !== 1 || !data.tokens || typeof data.tokens !== 'object' || Array.isArray(data.tokens)) {
        throw new Error('Choose a version 1 Mixtormat token export or an empty JSON file.');
      }
      // Validation completes before any token is applied; unknown keys are preserved on disk.
      ui.applyTokens(data.tokens);
      documentData = data; handle = selected;
      snapshot = current(); changed = true;
      button.textContent = 'Disconnect autosave';
      status(`Connected · ${handle.name}`);
      await save();
    } catch (error) {
      if (error.name !== 'AbortError') status(`Could not connect: ${error.message}`);
    } finally {
      button.disabled = false;
    }
  });

  new MutationObserver(() => {
    if (!handle) return;
    const next = current();
    if (next === snapshot) return; // Derived gradients do not count as authored edits.
    snapshot = next; changed = true;
    status(`Unsaved changes · ${handle.name}`);
    clearTimeout(timer); timer = setTimeout(save, 500);
  }).observe(document.documentElement, { attributes: true, attributeFilter: ['style'] });

  window.addEventListener('beforeunload', event => {
    if (!changed && !saving) return;
    event.preventDefault(); event.returnValue = '';
  });
})();

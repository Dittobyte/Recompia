// The Appearance section of the Layout panel: choose your own launcher picture, set how dark it is, or go back to the built-in one.
// The file is read in the page (background-image.js), only two small copies are kept (background-apply.js), nothing is uploaded.
import './background-panel.css';
import { DIM_MIN, DIM_MAX, setDim, setPicture, clearPicture, isDefault } from './background-model.js';
import { BACKGROUND_EVENT, currentBackground, setBackground } from './background-apply.js';
import { makeBackground } from './background-image.js';
import { esc, slider } from './ui.js';

const ACCEPT = 'image/png,image/jpeg,image/webp';
const NOT_SAVED = 'Your picture could not be saved on this PC. It will be used until the launcher closes.';

let status = '';        // the one-line message; kept here so a redraw of the panel does not lose it
let shown = false;      // whether the section was drawn at all (Testing channel only)

/** The section as HTML; '' when the picture feature is not offered on this channel. */
export function appearanceSection(b, { available } = {}) {
  shown = !!available;
  if (!available) return '';
  const custom = b.mode === 'custom';
  return `<section class="lp-sec bg-sec" id="bg-section">
    <h3>Appearance</h3>
    <p class="bg-help">Use a picture from this PC as the launcher background. It stays on this PC and is never uploaded.</p>
    <div class="bg-preview ${custom ? '' : 'is-empty'}">${custom
      ? `<img src="${esc(b.sharp)}" alt="Preview of your launcher background" />`
      : '<span>Built-in picture</span>'}</div>
    <div class="bg-row">
      <button class="btn" type="button" id="bg-choose">Choose picture...</button>
      <input type="file" id="bg-file" accept="${ACCEPT}" hidden tabindex="-1" aria-hidden="true" />
      <button class="btn" type="button" id="bg-reset" aria-label="Reset the background to default" ${isDefault(b) && !custom ? 'disabled' : ''}>Reset to default</button>
    </div>
    <div class="bg-row bg-dimrow"><label class="lp-name" for="bg-dim">Dim</label>${slider('bg-dim', DIM_MIN, DIM_MAX, b.dim, { step: 5, suffix: '%', label: 'Dim the picture' })}</div>
    <p class="bg-status" id="bg-status" role="status" aria-live="polite">${esc(status)}</p>
  </section>`;
}

/** Wire the section inside the dialog `root`. Works across redraws (delegated). Returns a function that unwires it. */
export function bindAppearance(root, { onChange } = {}) {
  status = '';
  let busy = false;
  let own = false;   // true while this section itself caused the change, so it is not redrawn under the player's hand

  const say = (text) => {
    status = text;
    const el = root.querySelector('#bg-status');
    if (el) el.textContent = text;
  };
  const apply = (next, quiet = false) => {
    own = quiet;
    try { return setBackground(next); } finally { own = false; }
  };
  const done = (res) => { if (res && res.stored === false) say(NOT_SAVED); onChange?.(res?.background); };
  const setBusy = (on) => {
    busy = on;
    for (const id of ['bg-choose', 'bg-reset']) {
      const el = root.querySelector(`#${id}`);
      if (el) el.disabled = on || (id === 'bg-reset' && isDefault(currentBackground()));
    }
  };

  const onClick = (e) => {
    const b = e.target.closest('button');
    if (b?.id === 'bg-choose') root.querySelector('#bg-file')?.click();
    else if (b?.id === 'bg-reset' && !busy) { say(''); done(apply(clearPicture(setDim(currentBackground(), 35)))); }
  };
  const onChangeEvt = async (e) => {
    if (e.target.id !== 'bg-file') return;
    const file = e.target.files?.[0];
    e.target.value = '';   // choosing the same file again must fire again
    if (!file || busy) return;
    setBusy(true);
    say('Preparing your picture...');
    try {
      const pair = await makeBackground(file);
      say('');
      const res = apply(setPicture(currentBackground(), pair));
      done(res);
      if (res.stored !== false) say('Your picture is set.');
    } catch (err) {
      say(err?.message || 'That picture could not be opened.');
    } finally {
      setBusy(false);
    }
  };
  const onInput = (e) => {
    if (e.target.id !== 'bg-dim') return;
    done(apply(setDim(currentBackground(), +e.target.value), true));   // quiet: the slider is in the player's hand
    const reset = root.querySelector('#bg-reset');
    if (reset && !busy) reset.disabled = isDefault(currentBackground());
  };
  const onEvent = (e) => {
    if (own || !shown) return;
    const old = root.querySelector('#bg-section');
    if (!old) return;
    const id = root.contains(document.activeElement) && old.contains(document.activeElement) ? document.activeElement.id : '';
    const tmp = document.createElement('div');
    tmp.innerHTML = appearanceSection(e.detail || currentBackground(), { available: true });
    old.replaceWith(...tmp.childNodes);
    if (id) {
      let el = root.querySelector(`#${id}`);
      if (el?.disabled) el = root.querySelector('#bg-choose');   // Reset just became unavailable
      el?.focus();
    }
  };

  root.addEventListener('click', onClick);
  root.addEventListener('change', onChangeEvt);
  root.addEventListener('input', onInput);
  window.addEventListener(BACKGROUND_EVENT, onEvent);
  return () => {
    root.removeEventListener('click', onClick);
    root.removeEventListener('change', onChangeEvt);
    root.removeEventListener('input', onInput);
    window.removeEventListener(BACKGROUND_EVENT, onEvent);
  };
}

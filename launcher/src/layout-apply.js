// Applies the player's layout choice to the page and keeps it in localStorage (per PC).
// Storage can throw (private mode, blocked): every access is in try/catch and the page works without it.
import { parse, serialize, normalize, DEFAULT_LAYOUT } from './layout-model.js';

export const LAYOUT_EVENT = 'layout-change';
const KEY = 'layout';
let current = normalize(DEFAULT_LAYOUT);

export function loadLayout() {
  try {
    const text = localStorage.getItem(KEY);
    return text == null ? normalize(DEFAULT_LAYOUT) : parse(text);
  } catch {
    return normalize(DEFAULT_LAYOUT);
  }
}

export function saveLayout(l) {
  try { localStorage.setItem(KEY, serialize(l)); } catch { /* the choice lasts until the launcher closes */ }
}

export function applyLayout(l) {
  const lay = normalize(l);
  current = lay;
  const root = document.documentElement;
  root.dataset.nav = lay.nav;
  root.dataset.density = lay.density;
  if (lay.blocks.news) root.removeAttribute('data-hide-news'); else root.setAttribute('data-hide-news', '');
  if (lay.blocks.link) root.removeAttribute('data-hide-link'); else root.setAttribute('data-hide-link', '');
  for (const btn of document.querySelectorAll('#nav [data-page]')) {
    const i = lay.order.indexOf(btn.dataset.page);
    if (i >= 0) btn.style.order = String(i);
    btn.hidden = lay.hidden.includes(btn.dataset.page);
  }
}

export function currentLayout() { return current; }

export function setLayout(l) {
  const lay = normalize(l);
  saveLayout(lay);
  applyLayout(lay);
  try { window.dispatchEvent(new CustomEvent(LAYOUT_EVENT, { detail: lay })); } catch { /* no window */ }
  return lay;
}

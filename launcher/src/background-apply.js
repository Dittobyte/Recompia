// Applies the player's own background to the page and keeps it in localStorage (per PC).
// Storage can throw (private mode, blocked, quota): every access is in try/catch and the page works without it.
import { parse, serialize, normalize, DEFAULT_BACKGROUND } from './background-model.js';

export const BACKGROUND_EVENT = 'background-change';
const KEY = 'background';
const PROPS = ['--art-url', '--art-soft-url', '--art-position', '--art-dim'];
let current = normalize(DEFAULT_BACKGROUND);

export function loadBackground() {
  try {
    const text = localStorage.getItem(KEY);
    return text == null ? normalize(DEFAULT_BACKGROUND) : parse(text);
  } catch {
    return normalize(DEFAULT_BACKGROUND);
  }
}

export function saveBackground(b) {
  try { localStorage.setItem(KEY, serialize(b)); return true; } catch { return false; }
}

export function applyBackground(b) {
  const bg = normalize(b);
  current = bg;
  if (typeof document === 'undefined' || !document.documentElement) return;
  const root = document.documentElement;
  if (bg.mode === 'custom') {
    root.style.setProperty('--art-url', `url("${bg.sharp}")`);
    root.style.setProperty('--art-soft-url', `url("${bg.soft}")`);
    root.style.setProperty('--art-position', '50% 50%');
    root.style.setProperty('--art-dim', String(bg.dim / 100));
    root.dataset.bg = 'custom';
  } else {
    for (const p of PROPS) root.style.removeProperty(p);
    delete root.dataset.bg;
  }
}

export function currentBackground() { return current; }

export function setBackground(b) {
  const bg = normalize(b);
  const stored = saveBackground(bg);
  applyBackground(bg);
  try { window.dispatchEvent(new CustomEvent(BACKGROUND_EVENT, { detail: bg })); } catch { /* no window */ }
  return { background: bg, stored };
}

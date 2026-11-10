// Pure layout model for the launcher's "Layout options" (no DOM, no storage). Tested by scripts/test-layout-model.mjs.
export const DEFAULT_LAYOUT = { v: 1, nav: 'left', density: 'comfortable', order: ['home', 'linkplay', 'news', 'settings'], hidden: [], blocks: { news: true, link: true } };
export const LOCKED = ['home', 'settings'];
export const NAV_POSITIONS = ['left', 'right', 'top', 'bottom', 'icons'];
export const DENSITIES = ['comfortable', 'compact'];
export const BLOCKS = ['news', 'link'];

const isObj = (x) => x !== null && typeof x === 'object' && !Array.isArray(x);
const strs = (x) => (Array.isArray(x) ? x.filter((s) => typeof s === 'string') : []);
const copyDefault = () => normalize(null);

// Anything in -> a valid, fresh layout out. The input is never changed.
export function normalize(raw) {
  const r = isObj(raw) ? raw : {};
  const ids = DEFAULT_LAYOUT.order;
  const order = [];
  for (const id of strs(r.order)) if (ids.includes(id) && !order.includes(id)) order.push(id);
  for (const id of ids) if (!order.includes(id)) order.push(id);
  const hidden = [];
  for (const id of strs(r.hidden)) if (ids.includes(id) && !LOCKED.includes(id) && !hidden.includes(id)) hidden.push(id);
  const b = isObj(r.blocks) ? r.blocks : {};
  const blocks = {};
  for (const k of BLOCKS) blocks[k] = typeof b[k] === 'boolean' ? b[k] : DEFAULT_LAYOUT.blocks[k];
  return {
    v: 1,
    nav: NAV_POSITIONS.includes(r.nav) ? r.nav : DEFAULT_LAYOUT.nav,
    density: DENSITIES.includes(r.density) ? r.density : DEFAULT_LAYOUT.density,
    order, hidden, blocks,
  };
}

export function setNav(l, v) { const n = normalize(l); if (NAV_POSITIONS.includes(v)) n.nav = v; return n; }
export function setDensity(l, v) { const n = normalize(l); if (DENSITIES.includes(v)) n.density = v; return n; }

// delta -1 moves up, +1 moves down; clamped at both ends.
export function moveItem(l, id, delta) {
  const n = normalize(l);
  const i = n.order.indexOf(id);
  if (i < 0 || typeof delta !== 'number' || !Number.isFinite(delta)) return n;
  const j = Math.max(0, Math.min(n.order.length - 1, i + Math.sign(delta)));
  n.order.splice(i, 1);
  n.order.splice(j, 0, id);
  return n;
}

export function setHidden(l, id, hidden) {
  const n = normalize(l);
  if (!n.order.includes(id) || LOCKED.includes(id)) return n;
  n.hidden = n.hidden.filter((h) => h !== id);
  if (hidden) n.hidden.push(id);
  return normalize(n);
}

export function setBlock(l, id, on) { const n = normalize(l); if (BLOCKS.includes(id)) n.blocks[id] = !!on; return n; }

export function isDefault(l) { return JSON.stringify(normalize(l)) === JSON.stringify(DEFAULT_LAYOUT); }
export function parse(text) { try { return normalize(JSON.parse(text)); } catch { return copyDefault(); } }
export function serialize(l) { return JSON.stringify(normalize(l)); }

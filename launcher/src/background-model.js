// Pure model of the player's own launcher background: no DOM, no storage, no canvas.
// Only two downscaled JPEG data URLs are kept; the original file is never stored.
export const BG_VERSION = 1;
export const DIM_MIN = 20, DIM_MAX = 70, DIM_DEFAULT = 35;          // percent of black over the picture
export const MAX_INPUT_BYTES = 25 * 1024 * 1024, MAX_INPUT_SIDE = 12000;
export const SHARP_W = 1920, SHARP_H = 1200, SOFT_W = 960, SOFT_H = 600;
export const MAX_STORED_CHARS = 1_500_000;                           // per data URL
export const ACCEPT_TYPES = ['image/png', 'image/jpeg', 'image/webp'];
export const DEFAULT_BACKGROUND = { v: 1, mode: 'builtin', dim: 35, sharp: '', soft: '' };

const PREFIX = 'data:image/jpeg;base64,';

function cleanDim(d) {
  if (typeof d !== 'number' || !Number.isFinite(d)) return DIM_DEFAULT;
  return Math.min(DIM_MAX, Math.max(DIM_MIN, Math.round(d)));
}

function validUrl(s) {
  return typeof s === 'string' && s.startsWith(PREFIX) && s.length > PREFIX.length && s.length <= MAX_STORED_CHARS;
}

export function normalize(raw) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw) || raw.v !== BG_VERSION) return { ...DEFAULT_BACKGROUND };
  const dim = cleanDim(raw.dim);
  if (raw.mode === 'custom' && validUrl(raw.sharp) && validUrl(raw.soft)) {
    return { v: BG_VERSION, mode: 'custom', dim, sharp: raw.sharp, soft: raw.soft };
  }
  return { v: BG_VERSION, mode: 'builtin', dim, sharp: '', soft: '' };
}

export function parse(text) {
  try { return normalize(JSON.parse(text)); } catch { return { ...DEFAULT_BACKGROUND }; }
}

export function serialize(b) { return JSON.stringify(normalize(b)); }

export function setDim(b, dim) { return normalize({ ...normalize(b), dim }); }

export function setPicture(b, pic) {
  const base = normalize(b);
  const p = pic && typeof pic === 'object' ? pic : {};
  return normalize({ ...base, mode: 'custom', sharp: p.sharp, soft: p.soft });
}

export function clearPicture(b) { return normalize({ ...normalize(b), mode: 'builtin', sharp: '', soft: '' }); }

export function isDefault(b) {
  const n = normalize(b);
  return n.mode === DEFAULT_BACKGROUND.mode && n.dim === DEFAULT_BACKGROUND.dim && n.sharp === '' && n.soft === '';
}

export function checkInput({ size, type, width, height } = {}) {
  if (!ACCEPT_TYPES.includes(type)) return { ok: false, reason: 'That file is not a PNG, JPEG or WebP picture.' };
  if (typeof size === 'number' && size > MAX_INPUT_BYTES) return { ok: false, reason: 'That picture is bigger than 25 MB.' };
  if ((typeof width === 'number' && width > MAX_INPUT_SIDE) || (typeof height === 'number' && height > MAX_INPUT_SIDE)) {
    return { ok: false, reason: 'That picture is larger than 12000 pixels on one side.' };
  }
  return { ok: true };
}

// Centered source rectangle with the aspect of dst (cover crop); integers, always inside the source.
export function coverRect(srcW, srcH, dstW, dstH) {
  const W = Math.max(1, Math.floor(srcW)), H = Math.max(1, Math.floor(srcH));
  let sw = W, sh = H;
  if (W * dstH > H * dstW) sw = Math.max(1, Math.min(W, Math.round(H * dstW / dstH)));   // source wider than dst: crop the sides
  else sh = Math.max(1, Math.min(H, Math.round(W * dstH / dstW)));                       // source taller: crop top and bottom
  return { sx: Math.floor((W - sw) / 2), sy: Math.floor((H - sh) / 2), sw, sh };
}

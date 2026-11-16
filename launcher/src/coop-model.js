// Co-op profile helpers: private room codes, relay address validation, mode filtering. Pure: no DOM, no Tauri.
import { t } from './i18n.js';
import { validAddr } from './linkplay-model.js';

const B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const ROOM_LEN = 16;

/** Normalizes a Crockford base32 room code into `XXXX-XXXX-XXXX-XXXX`, or '' when invalid. */
export function normalizeRoomCode(raw) {
  let out = '';
  for (const ch of String(raw ?? '')) {
    if (ch === '-' || ch === ' ') continue;
    let up = ch.toUpperCase();
    if (up === 'O') up = '0';
    else if (up === 'I' || up === 'L') up = '1';
    if (!B32.includes(up) || out.length === ROOM_LEN) return '';
    out += up;
  }
  if (out.length !== ROOM_LEN) return '';
  return `${out.slice(0, 4)}-${out.slice(4, 8)}-${out.slice(8, 12)}-${out.slice(12, 16)}`;
}

/** Validation message for a room code; '' when valid (or empty when `optional` is true). */
export function roomCodeProblem(raw, { optional = false } = {}) {
  const s = String(raw ?? '').trim();
  if (!s) return optional ? '' : t('Type a 16-character room code (XXXX-XXXX-XXXX-XXXX).');
  return normalizeRoomCode(s) ? '' : t('Type a 16-character room code (XXXX-XXXX-XXXX-XXXX).');
}

/** Validation message for an optional co-op relay address (`<IPv4>[:port]`), '' when empty or valid. */
export function relayProblem(raw) {
  const s = String(raw ?? '').trim();
  if (!s) return '';
  return validAddr(s) ? '' : t('Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.');
}

export const isCoopProfile = (p) => !!p?.coop;

export function activeProfileOf(store) {
  const list = store?.profiles ?? [];
  return list.find((p) => p.id === store?.active) ?? list[0] ?? null;
}

export const coopProfiles = (store) => (store?.profiles ?? []).filter(isCoopProfile);

export const singleplayerProfiles = (store) => (store?.profiles ?? []).filter((p) => !isCoopProfile(p));

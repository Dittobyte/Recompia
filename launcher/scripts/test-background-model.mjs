// Tests for the pure background model (src/background-model.js). Run: node launcher/scripts/test-background-model.mjs
import * as M from '../src/background-model.js';
const { DEFAULT_BACKGROUND: D, normalize, parse, serialize, setDim, setPicture, clearPicture, isDefault, checkInput, coverRect } = M;

let bad = 0, n = 0;
const check = (ok, what) => { n++; if (!ok) { console.log(`FAIL ${what}`); bad++; } };
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const deep = (o) => { Object.values(o).forEach((v) => { if (v && typeof v === 'object') deep(v); }); return Object.freeze(o); };

const P = 'data:image/jpeg;base64,';
const S = P + 'AAAA', T = P + 'BBBB';
const custom = { v: 1, mode: 'custom', dim: 50, sharp: S, soft: T };

check(eq(normalize(D), D) && isDefault(D), 'default is valid and isDefault');
check(M.BG_VERSION === 1 && M.DIM_MIN === 20 && M.DIM_MAX === 70 && M.DIM_DEFAULT === 35 && M.MAX_STORED_CHARS === 1_500_000 && eq(M.ACCEPT_TYPES, ['image/png', 'image/jpeg', 'image/webp']), 'constants');
for (const x of [null, undefined, 5, 'x', [], {}, true]) check(eq(normalize(x), D), `normalize(${JSON.stringify(x)}) is default`);
check(normalize(D) !== D && normalize(custom) !== custom, 'result is a fresh object');
check(eq(normalize(custom), custom), 'valid custom kept');
check(!('extra' in normalize({ ...custom, extra: 1 })) && Object.keys(normalize({ ...custom, extra: 1, z: 2 })).length === 5, 'unknown fields dropped');
check(normalize({ ...custom, mode: 'weird' }).mode === 'builtin' && normalize({ ...custom, mode: 'weird' }).sharp === '', 'bad mode gives builtin');
check(normalize({ ...custom, mode: 'weird' }).dim === 50, 'dim kept when mode is bad');
check(normalize({ ...custom, sharp: 'http://x/a.jpg' }).mode === 'builtin', 'non-data URL sharp rejected');
check(normalize({ ...custom, soft: 'data:image/png;base64,AAAA' }).mode === 'builtin', 'non-jpeg data URL soft rejected');
check(normalize({ ...custom, sharp: P }).mode === 'builtin', 'prefix only is rejected');
check(normalize({ ...custom, sharp: P + 'AAAA") ; x' }).mode === 'builtin' && normalize({ ...custom, soft: P + 'AA"AA' }).mode === 'builtin' && normalize({ ...custom, sharp: P + 'AA AA' }).mode === 'builtin' && normalize({ ...custom, soft: P + 'AAAA\n' }).mode === 'builtin', 'anything but base64 after the prefix is rejected');
check(normalize({ ...custom, sharp: P + 'QUJD+/8=' }).mode === 'custom', 'valid base64 with + / and padding is kept');
check(normalize({ ...custom, soft: 7 }).mode === 'builtin' && normalize({ v: 1, mode: 'custom' }).mode === 'builtin', 'non-string or missing URL rejected');
check(normalize({ ...custom, sharp: P + 'A'.repeat(M.MAX_STORED_CHARS - P.length) }).mode === 'custom', 'exactly at the limit is kept');
check(normalize({ ...custom, sharp: P + 'A'.repeat(M.MAX_STORED_CHARS - P.length + 1) }).mode === 'builtin', 'oversize rejected');
check(normalize({ ...custom, soft: P + 'A'.repeat(M.MAX_STORED_CHARS) }).soft === '', 'oversize clears both URLs');
check(normalize({ ...custom, dim: 'a lot' }).dim === 35 && normalize({ ...custom, dim: NaN }).dim === 35 && normalize({ ...custom, dim: Infinity }).dim === 35 && normalize({ ...custom, dim: null }).dim === 35, 'non-number dim gives default');
check(normalize({ ...custom, dim: 5 }).dim === 20 && normalize({ ...custom, dim: 99 }).dim === 70 && normalize({ ...custom, dim: 44.6 }).dim === 45, 'dim clamped and rounded');
check(eq(normalize({ ...custom, v: 2 }), D) && eq(normalize({ ...custom, v: '1' }), D) && eq(normalize({ mode: 'custom', sharp: S, soft: T }), D), 'wrong or missing v gives default');
const frozen = deep(JSON.parse(JSON.stringify(custom)));
let threw = false;
try { normalize(frozen); setDim(frozen, 40); setPicture(frozen, { sharp: S, soft: T }); clearPicture(frozen); serialize(frozen); isDefault(frozen); } catch { threw = true; }
check(!threw, 'frozen input is never mutated');
const snap = JSON.stringify(custom); normalize(custom); setDim(custom, 60); clearPicture(custom);
check(JSON.stringify(custom) === snap, 'input unchanged after calls');
check(eq(parse('{not json'), D) && eq(parse(''), D) && eq(parse(undefined), D) && eq(parse('null'), D) && eq(parse('[1]'), D) && eq(parse('"x"'), D), 'garbage parses to default');
check(parse('x') !== D && eq(parse(serialize(custom)), custom), 'parse gives a copy; round trip');
check(eq(parse(JSON.stringify({ ...custom, dim: 3 })).dim, 20), 'parse normalizes');

check(setDim(D, 10).dim === 20 && setDim(D, 100).dim === 70 && setDim(D, 55).dim === 55 && setDim(D, 'x').dim === 35, 'setDim clamps');
check(setDim(custom, 60).mode === 'custom' && setDim(custom, 60).sharp === S && setDim(custom, 60) !== custom, 'setDim keeps picture, new object');
check(setDim(null, 40).dim === 40 && setDim(null, 40).mode === 'builtin', 'setDim on garbage');
const sp = setPicture(D, { sharp: S, soft: T });
check(sp.mode === 'custom' && sp.sharp === S && sp.soft === T && sp.dim === 35, 'setPicture valid pair');
check(setPicture(custom, { sharp: S, soft: T }).dim === 50, 'setPicture keeps dim');
check(setPicture(custom, { sharp: 'nope', soft: T }).mode === 'builtin' && setPicture(custom, { sharp: S }).mode === 'builtin' && setPicture(custom, null).mode === 'builtin' && setPicture(custom, {}).sharp === '', 'setPicture invalid pair gives builtin');
const cl = clearPicture(custom);
check(cl.mode === 'builtin' && cl.sharp === '' && cl.soft === '' && cl.dim === 50, 'clearPicture keeps dim');
check(!isDefault(custom) && !isDefault(setDim(D, 50)) && isDefault({}) && isDefault(clearPicture(setDim(D, 35))) && !isDefault(clearPicture(custom)), 'isDefault');

check(eq(checkInput({ size: 1000, type: 'image/png' }), { ok: true }) && checkInput({ size: 1, type: 'image/jpeg' }).ok && checkInput({ size: 1, type: 'image/webp', width: 12000, height: 12000 }).ok, 'checkInput accepts good input');
check(checkInput({ size: 1, type: 'image/gif' }).reason === 'That file is not a PNG, JPEG or WebP picture.' && !checkInput({ size: 1, type: '' }).ok && !checkInput({ size: 1 }).ok && !checkInput().ok, 'checkInput bad type');
check(checkInput({ size: M.MAX_INPUT_BYTES, type: 'image/png' }).ok && checkInput({ size: M.MAX_INPUT_BYTES + 1, type: 'image/png' }).reason === 'That picture is bigger than 25 MB.', 'checkInput size limit');
check(checkInput({ size: 1, type: 'image/png', width: 12001, height: 10 }).reason === 'That picture is larger than 12000 pixels on one side.' && !checkInput({ size: 1, type: 'image/png', width: 10, height: 12001 }).ok, 'checkInput side limit');

const inside = (r, w, h) => [r.sx, r.sy, r.sw, r.sh].every(Number.isInteger) && r.sx >= 0 && r.sy >= 0 && r.sw >= 1 && r.sh >= 1 && r.sx + r.sw <= w && r.sy + r.sh <= h;
check(eq(coverRect(4000, 1200, 1920, 1200), { sx: 1040, sy: 0, sw: 1920, sh: 1200 }), 'coverRect wide source crops the sides');
{
  const r = coverRect(1000, 3000, 1920, 1200);
  check(r.sx === 0 && r.sw === 1000 && r.sh === 625 && r.sy === Math.floor((3000 - 625) / 2), 'coverRect tall source crops top and bottom');
}
check(eq(coverRect(1920, 1200, 1920, 1200), { sx: 0, sy: 0, sw: 1920, sh: 1200 }) && eq(coverRect(960, 600, 1920, 1200), { sx: 0, sy: 0, sw: 960, sh: 600 }), 'coverRect equal aspect keeps all');
check(eq(coverRect(1000, 1000, 1920, 1200), { sx: 0, sy: 187, sw: 1000, sh: 625 }), 'coverRect square source');
for (const [w, h] of [[1, 1], [2, 1], [1, 2], [3, 7], [100, 1], [1, 100], [12000, 11999], [7, 5]]) check(inside(coverRect(w, h, 1920, 1200), w, h) && inside(coverRect(w, h, 960, 600), w, h), `coverRect ${w}x${h} stays inside`);
{ const r = coverRect(1, 1, 1920, 1200); check(r.sw === 1 && r.sh === 1 && r.sx === 0 && r.sy === 0, 'coverRect 1x1 tiny'); }
{ const r = coverRect(3, 100, 1920, 1200); check(r.sw === 3 && r.sh === 2, 'coverRect thin tall tiny'); }

if (bad) { console.log(`${bad} of ${n} FAILED`); process.exit(1); }
console.log(`background-model: ${n} checks passed`);

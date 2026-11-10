// Tests for the pure layout model (src/layout-model.js). Run: node launcher/scripts/test-layout-model.mjs
import { DEFAULT_LAYOUT as D, normalize, setNav, setDensity, moveItem, setHidden, setBlock, isDefault, parse, serialize } from '../src/layout-model.js';

let bad = 0, n = 0;
const check = (ok, what) => { n++; if (!ok) { console.log(`FAIL ${what}`); bad++; } };
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const deep = (o) => { Object.values(o).forEach((v) => { if (v && typeof v === 'object') deep(v); }); return Object.freeze(o); };

check(eq(normalize(D), D) && isDefault(D), 'default is valid and isDefault');
for (const x of [null, undefined, 5, 'x', [], {}, true]) check(eq(normalize(x), D), `normalize(${JSON.stringify(x)}) is default`);
check(normalize(D) !== D && normalize(D).order !== D.order, 'result is a fresh object');
check(normalize({ nav: 'diagonal', density: 'huge' }).nav === 'left' && normalize({ nav: 'diagonal', density: 'huge' }).density === 'comfortable', 'unknown nav and density fall back');
check(normalize({ nav: 'top', density: 'compact' }).nav === 'top' && normalize({ density: 'compact' }).density === 'compact', 'valid nav and density kept');
check(eq(normalize({ order: ['settings', 'bogus', 'home'] }).order, ['settings', 'home', 'linkplay', 'news']), 'unknown ids dropped, missing appended in default order');
check(eq(normalize({ order: ['news', 'news', 'home', 'home'] }).order, ['news', 'home', 'linkplay', 'settings']), 'duplicates removed');
check(eq(normalize({ hidden: ['home', 'settings', 'news', 'news', 'zzz'] }).hidden, ['news']), 'locked never hidden, unknown and duplicate dropped');
check(eq(setHidden(D, 'home', true).hidden, []) && eq(setHidden(D, 'settings', true), D), 'hiding a locked id is a no-op');
check(eq(setHidden(D, 'news', true).hidden, ['news']) && eq(setHidden(setHidden(D, 'news', true), 'news', false).hidden, []), 'hide and show a normal id');
check(eq(moveItem(D, 'home', -1).order, D.order), 'move up at the top clamps');
check(eq(moveItem(D, 'settings', 1).order, D.order), 'move down at the bottom clamps');
check(eq(moveItem(D, 'news', -1).order, ['home', 'news', 'linkplay', 'settings']), 'move up by one');
check(eq(moveItem(D, 'linkplay', 1).order, ['home', 'news', 'linkplay', 'settings']), 'move down by one');
check(eq(moveItem(D, 'nope', 1), D), 'moving an unknown id changes nothing');
check(setBlock(D, 'news', false).blocks.news === false && setBlock(D, 'link', false).blocks.link === false && setBlock(D, 'bogus', false).blocks.news === true, 'setBlock');
check(setNav(D, 'right').nav === 'right' && eq(setNav(D, 'bogus'), D) && setDensity(D, 'compact').density === 'compact' && eq(setDensity(D, 'x'), D), 'setNav and setDensity');
check(!isDefault(setNav(D, 'top')) && isDefault({}), 'isDefault');
const messy = { nav: 'top', order: ['news', 'x', 'news'], hidden: ['home', 'linkplay'], blocks: { news: false, link: 'yes' } };
check(eq(normalize(normalize(messy)), normalize(messy)), 'idempotent');
const frozen = deep(JSON.parse(JSON.stringify(messy)));
let threw = false;
try { normalize(frozen); setNav(frozen, 'left'); setDensity(frozen, 'compact'); moveItem(frozen, 'news', 1); setHidden(frozen, 'news', true); setBlock(frozen, 'link', true); serialize(frozen); } catch { threw = true; }
check(!threw, 'frozen inputs are never mutated');
check(eq(parse('{not json'), D) && eq(parse(''), D) && eq(parse(undefined), D) && eq(parse('null'), D), 'bad JSON gives the default');
check(eq(parse(serialize(normalize(messy))), normalize(messy)), 'serialize/parse round trip');

if (bad) { console.log(`${bad} of ${n} FAILED`); process.exit(1); }
console.log(`layout-model: ${n} checks passed`);

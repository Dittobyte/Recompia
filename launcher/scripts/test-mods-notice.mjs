// Run: node scripts/test-mods-notice.mjs (no browser, no Tauri). The mods notice model and its Settings row.
import assert from 'node:assert/strict';
import { FIELDS, SECTIONS, gameplayKeys, readAll } from '../src/settings-schema.js';
import { MOD_LABELS, modsNotice } from '../src/mods-notice-model.js';

const EXP = ['menu_in_game', 'taller_fov'];
const gk = gameplayKeys();
for (const k of [...gk, ...EXP]) assert.ok(MOD_LABELS[k], `${k} has a notice label`);

const allOn = Object.fromEntries([...gk, ...EXP].map((k) => [k, '1']));
const allOff = Object.fromEntries([...gk, ...EXP].map((k) => [k, '0']));
assert.equal(modsNotice(allOff, gk, EXP).lines[0], 'Enabled gameplay mods: none.');
assert.equal(modsNotice(allOff, gk, EXP).lines[1], 'Experimental features enabled: none.');
assert.deepEqual(modsNotice({ ...allOff, camera_aim: '1', craft_search: true }, gk, EXP).gameplay, ['Camera aim', 'Search']);
assert.deepEqual(modsNotice({ ...allOff, menu_in_game: '1' }, gk, EXP).experimental, ["Mouse in the game's menus"]);
assert.equal(modsNotice(allOn, gk, EXP).gameplay.length, gk.length);
assert.equal(modsNotice({}, gk).lines.length, 1);   // no experimental keys given: the category is not claimed

// the real defaults (every gameplay mod on) through the schema, and the row is in Game > Gameplay
const empty = { pkp: {}, controls: {}, config: { channel: 'stable' }, controls_exists: false };
const v = readAll(empty);
assert.equal(modsNotice(v, gk, EXP).gameplay.length, gk.length);
const row = SECTIONS.flatMap((s) => s.groups).flatMap((g) => g.rows).find((r) => r.id === 'mods_notice');
assert.ok(row && row.type === 'note', 'mods_notice note row exists');
assert.ok(row.note({}, v).text.startsWith('Enabled gameplay mods:'));
assert.ok(!FIELDS.mods_notice, 'the notice is not a settings field');
console.log('mods notice ok');

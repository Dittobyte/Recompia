// Mod review: which gameplay mods are on, what a mod set (Dev Recs, Vanilla) or a switch changes in pkp.ini, which experimental options the
// review offers, and when the home page asks the player to look. Pure: no DOM, no Tauri, so node runs it (scripts/test-mod-review.mjs).
// `pkp` is read_settings' pkp.ini map ("pkp.jump_momentum" -> "1"); a missing key reads as the runtime's default.
//
// Rule 6.1 of PROJECT.md holds here: the project never turns an extra off for the player. Only a Vanilla pick or a switch the player flips
// writes an off, and Dev Recs leaves the optional mods (opt_in) as they are instead of switching them off.
import { t } from './i18n.js';
import { gameplaySwitches } from './settings-schema.js';
import { interactEdits, readIni, storyEdits } from './linkplay-model.js';

const truthy = (v) => ['1', 'true', 'yes', 'on'].includes(String(v).trim().toLowerCase());
const pkpEdit = (section, key, on) => ({ file: 'pkp', section, key, value: on ? '1' : '0' });

/** Every gameplay mod switch with whether it is on now: [{ key, label, def, optIn, hidden, on }]. */
export function modsNow(pkp) {
  return gameplaySwitches().map((m) => {
    const raw = pkp[`pkp.${m.key}`];
    return { ...m, on: raw == null || String(raw).trim() === '' ? m.def : truthy(raw) };
  });
}

/** The mods the home page asks about: on, visible in the launcher and not yet in the reviewed list. */
export const unreviewed = (mods, reviewed) => mods.filter((m) => m.on && !m.hidden && !reviewed.includes(m.key));

/** What a mod set wants of every switch ({ key: on }). Dev Recs: every mod on except the optional ones, which stay as they are. Vanilla: all off. */
export function setWants(set, mods) {
  return Object.fromEntries(mods.map((m) => [m.key, set === 'vanilla' ? false : m.optIn ? m.on : true]));
}

/** The experimental options (not gameplay mods): { id, label, read(pkp) -> on, edits(on) -> pkp.ini edits }. Linked: story co-op turns Interact everywhere on. */
export const EXPERIMENTAL = [
  { id: 'unpaced_loading', label: t('Fast loading screens'), read: (pkp) => truthy(pkp['display.unpaced_loading'] ?? '0'), edits: (on) => [pkpEdit('display', 'unpaced_loading', on)] },
  { id: 'link_interact', label: t('Visitors can build everywhere'), read: (pkp) => readIni(pkp).interact, edits: interactEdits },
  { id: 'link_story', label: t('Visitors can talk to story characters'), read: (pkp) => readIni(pkp).story, edits: storyEdits },
];

/** The state the dialog starts from: { mods: { key: on }, exp: { id: on } }. */
export const draftOf = (pkp) => ({
  mods: Object.fromEntries(modsNow(pkp).map((m) => [m.key, m.on])),
  exp: Object.fromEntries(EXPERIMENTAL.map((e) => [e.id, e.read(pkp)])),
});

/** Story co-op needs Interact everywhere, so the dialog keeps the two together. */
export function toggled(draft, id, on) {
  const exp = { ...draft.exp };
  if (id in exp) {
    exp[id] = on;
    if (id === 'link_story' && on) exp.link_interact = true;
  }
  return { mods: id in draft.mods ? { ...draft.mods, [id]: on } : draft.mods, exp };
}

/** The pkp.ini edits that turn the settings into the draft; nothing for what already reads the same. */
export function reviewEdits(pkp, draft) {
  const edits = modsNow(pkp).filter((m) => draft.mods[m.key] !== undefined && draft.mods[m.key] !== m.on).map((m) => pkpEdit('pkp', m.key, draft.mods[m.key]));
  for (const e of EXPERIMENTAL) if (draft.exp[e.id] !== e.read(pkp)) edits.push(...e.edits(draft.exp[e.id]));
  const last = new Map(edits.map((e) => [`${e.section}.${e.key}`, e]));   // story co-op and Interact everywhere both write one key
  return [...last.values()];
}

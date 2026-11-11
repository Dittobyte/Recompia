// Checks the mod review (src/mod-review-model.js) against the runtime's generated schema:
//   - the review lists every gameplay switch, a visible one has a label, and no switch is missing from Dev Recs or Vanilla;
//   - Dev Recs never switches an optional (opt_in) mod off and never leaves another mod off; Vanilla switches every mod off;
//   - a mod set, a switch or an experimental option writes edits only where the file differs, to keys the runtime has;
//   - the home page asks about a mod that is on, visible and not yet reviewed, and about nothing once the review is saved.
// Run: node scripts/test-mod-review.mjs   (exit 1 on a mismatch)
import { SCHEMAS, gameplayKeys, gameplaySwitches, isGameplaySwitch } from '../src/settings-schema.js';
import { EXPERIMENTAL, draftOf, modsNow, reviewEdits, setWants, toggled, unreviewed } from '../src/mod-review-model.js';

let bad = 0;
const check = (ok, what) => { if (!ok) { console.log(`FAIL ${what}`); bad++; } };
const keysOf = (edits) => edits.map((e) => `${e.section}.${e.key}=${e.value}`).sort().join(' ');
const pkpKeys = SCHEMAS.pkp.sections.find((s) => s.name === 'pkp').keys;

// the switches
const switches = gameplaySwitches();
const schemaSwitches = pkpKeys.filter(isGameplaySwitch);
check(switches.length === schemaSwitches.length && switches.length > 0, 'the review does not list every gameplay switch of the runtime');
for (const m of switches) {
  const k = schemaSwitches.find((s) => s.key === m.key);
  check(m.def === (k.default === '1') && m.optIn === !!k.opt_in && m.hidden === !!k.hidden, `switch ${m.key}: default, opt_in or hidden differs from the schema`);
  check(m.hidden || m.label !== m.key, `switch ${m.key} is shown but has no label`);
}
check(gameplayKeys().length >= switches.length, 'gameplayKeys() lost a key');

// a file with no keys reads as the runtime's defaults
const fresh = modsNow({});
for (const m of fresh) check(m.on === m.def, `${m.key}: an empty pkp.ini does not read as the default`);

// Dev Recs and Vanilla, from every starting point: all on, all off, and each mod alone flipped
const starts = [{}, ...switches.flatMap((m) => [{ [`pkp.${m.key}`]: '0' }, { [`pkp.${m.key}`]: '1' }])];
for (const pkp of starts) {
  const mods = modsNow(pkp);
  const dev = setWants('dev', mods);
  const vanilla = setWants('vanilla', mods);
  for (const m of mods) {
    check(vanilla[m.key] === false, `Vanilla leaves ${m.key} on`);
    check(m.optIn ? dev[m.key] === m.on : dev[m.key] === true, `Dev Recs ${m.optIn ? 'changes the optional' : 'does not turn on'} ${m.key}`);
  }
  const edits = reviewEdits(pkp, { ...draftOf(pkp), mods: dev });
  check(edits.every((e) => e.value === '1' && switches.some((m) => m.key === e.key && !m.optIn)), `Dev Recs wrote ${keysOf(edits)}: only mods that are not optional, and only to on`);
  const off = reviewEdits(pkp, { ...draftOf(pkp), mods: vanilla });
  check(off.every((e) => e.value === '0'), 'Vanilla wrote an on');
  check(modsNow(applied(pkp, off)).every((m) => !m.on), 'Vanilla did not leave every mod off');
}

function applied(pkp, edits) {
  const next = { ...pkp };
  for (const e of edits) next[`${e.section}.${e.key}`] = e.value;
  return next;
}

// nothing to write when nothing changed
check(reviewEdits({}, draftOf({})).length === 0, 'an untouched review writes edits');

// the experimental options
for (const e of EXPERIMENTAL) {
  check(e.read({}) === false, `${e.id} is not off in an empty pkp.ini`);
  const edits = reviewEdits({}, toggled(draftOf({}), e.id, true));
  check(edits.length > 0, `${e.id}: switching it on writes nothing`);
  for (const w of edits) {
    if (w.section === 'pkp') check(pkpKeys.some((k) => k.key === w.key), `${e.id} writes [pkp] ${w.key}, which the runtime does not have`);
  }
}
check(keysOf(reviewEdits({}, toggled(draftOf({}), 'link_story', true))).includes('link_interact_everywhere'), 'Story co-op does not switch Interact everywhere on');

// the notice
const mods = modsNow({});
const names = (list) => list.map((m) => m.key);
check(names(unreviewed(mods, [])).every((k) => switches.find((m) => m.key === k).def && !switches.find((m) => m.key === k).hidden), 'the notice asks about an off or hidden mod');
check(unreviewed(mods, switches.map((m) => m.key)).length === 0, 'the notice stays after the review was saved');
check(unreviewed(modsNow(applied({}, reviewEdits({}, { ...draftOf({}), mods: setWants('vanilla', mods) }))), []).length === 0, 'the notice asks about mods that are all off');
const some = switches.filter((m) => m.def && !m.hidden).map((m) => m.key);
check(unreviewed(mods, some.slice(1)).length === 1, 'a mod added after the review is not asked about once');

console.log(bad ? `${bad} failed` : `mod review: ${switches.length} switches ok`);
process.exit(bad ? 1 : 0);

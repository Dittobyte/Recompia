// The mod review: a dialog with every gameplay mod, the mod sets Dev Recs and Vanilla, and the experimental options. Opened from the notice on
// the home page; what it writes is worked out in mod-review-model.js. Mods are read when the game starts, so a change made while the game
// runs applies the next time: the dialog then offers to start the game again once it has been closed (the launcher cannot close it).
import './mod-review.css';
import { api } from '../api.js';
import { gamePhase } from '../action.js';
import { t } from '../i18n.js';
import { EXPERIMENTAL, draftOf, modsNow, reviewEdits, setWants, toggled } from '../mod-review-model.js';
import { store } from '../store.js';
import { confirm, errText, esc, group, row, segmented, toast, toggle } from '../ui.js';

const askExperimental = () => confirm({
  title: t('Switch on an experimental option?'),
  body: t('Experimental options are for testers. They can have bugs or stop working. If something goes wrong, tell us in Settings > About > Report a problem.'),
  ok: t('I understand, switch it on'),
});

/** The player picks what they want. Resolves { pkp, mods, draft } when they pressed Save, null for Later, Esc or a click outside. */
function ask(pkp, mods) {
  return new Promise((resolve) => {
    let draft = draftOf(pkp);
    let tab = 'mods';
    const visible = mods.filter((m) => !m.hidden);
    const dlg = document.createElement('dialog');
    dlg.className = 'modal glass strong mod-review';
    dlg.setAttribute('aria-labelledby', 'modal-title');
    dlg.innerHTML = `
      <div class="modal-body">
        <h2 id="modal-title">${esc(t('Review your gameplay mods'))}</h2>
        <p>${esc(t('Some of these were switched on for you by an earlier version. Choose what you want.'))}</p>
        ${segmented('mr-tab', [['mods', t('Gameplay mods')], ['exp', t('Experimental')]], tab, { label: t('Review your gameplay mods') })}
        <div id="mr-body"></div>
      </div>
      <div class="modal-actions">
        <button class="btn quiet" data-answer="">${esc(t('Later'))}</button>
        <button class="btn primary" data-answer="ok">${esc(t('Save'))}</button>
      </div>`;
    const body = dlg.querySelector('#mr-body');

    const modRow = (m) => row(m.label, `${m.optIn ? `<span class="chip">${esc(t('Optional'))}</span>` : ''}${toggle(`mr-${m.key}`, draft.mods[m.key], { label: m.label })}`);
    // story co-op needs Interact everywhere, so that one shows on and locked while story co-op is on (as on the Link Play page)
    const expRow = (e) => {
      const linked = e.id === 'link_interact' && draft.exp.link_story;
      return row(e.label, toggle(`mr-${e.id}`, draft.exp[e.id] || linked, { label: e.label, disabled: linked }));
    };
    const draw = () => {
      body.innerHTML = tab === 'mods'
        ? `<div class="mr-sets"><span>${esc(t('Mod set'))}</span>
            <button class="btn sm" data-set="dev">${esc(t('Dev Recs'))}</button>
            <button class="btn sm" data-set="vanilla">${esc(t('Vanilla'))}</button></div>
          <p class="mr-hint">${esc(t('Dev Recs: what the developers recommend. Every mod is on; optional ones stay as you have them. Vanilla: every mod off, the game plays as on the console.'))}</p>
          ${group('', visible.map(modRow))}`
        : `<p class="mr-hint">${esc(t('Experimental options are for testers. They can have bugs or stop working.'))}</p>
          ${group('', [...visible.filter((m) => m.optIn).map(modRow), ...EXPERIMENTAL.map(expRow)])}`;
    };
    draw();

    dlg.addEventListener('change', async (e) => {
      const el = e.target;
      if (el.name === 'mr-tab') {
        tab = el.value;
        return draw();
      }
      if (!el.id?.startsWith('mr-')) return;
      const id = el.id.slice(3);
      const experimental = tab === 'exp';   // an optional mod asks only where it is listed as experimental
      if (el.checked && experimental && !(await askExperimental())) return draw();
      draft = toggled(draft, id, el.checked);
      draw();
    });
    dlg.addEventListener('click', (e) => {
      const set = e.target.closest('[data-set]')?.dataset.set;
      if (set) {
        draft = { ...draft, mods: setWants(set, mods.map((m) => ({ ...m, on: draft.mods[m.key] }))) };
        return draw();
      }
      const answer = e.target.closest('[data-answer]')?.dataset.answer;
      if (answer !== undefined) dlg.close(answer);
      else if (e.target === dlg) dlg.close('');
    });
    dlg.addEventListener('close', () => {
      dlg.remove();
      resolve(dlg.returnValue === 'ok' ? { pkp, mods, draft } : null);
    });
    document.body.appendChild(dlg);
    dlg.showModal();
    dlg.querySelector('[data-answer="ok"]').focus();
  });
}

/** Starts the game again once the running one has exited, unless it crashed. */
function startAgainAfterExit() {
  const off = store.subscribe((s) => {
    if (s.game?.state !== 'exited') return;
    off();
    if (s.game.crashed) return;
    toast(t('Starting the game again'), 'ok');
    store.play();
  });
}

async function afterSave(changed) {
  if (!changed) return toast(t('Saved'), 'ok');
  if (gamePhase(store.state.game) !== 'running') return toast(t('Saved. The mods apply the next time the game starts.'), 'ok');
  const again = await confirm({
    title: t('Start the game again?'),
    body: t('The game is running. The mods apply the next time it starts. Close the game when you are ready and the launcher starts it again.'),
    ok: t('Start again when I close it'),
    cancel: t('No thanks'),
  });
  if (again) startAgainAfterExit();
  else toast(t('Saved. The mods apply the next time the game starts.'), 'ok');
}

/** Opens the review. Resolves true when it was saved (the notice on the home page is then done), false for Later or an error. */
export async function reviewMods() {
  let pkp;
  try {
    pkp = (await api.readSettings()).pkp;
  } catch (e) {
    toast(errText(e), 'error');
    return false;
  }
  const mods = modsNow(pkp);
  const picked = await ask(pkp, mods);
  if (!picked) return false;
  const edits = reviewEdits(pkp, picked.draft);
  try {
    if (edits.length) await api.writeSettings({ edits });
    await api.saveConfig({ config: { ...(await api.getConfig()), mods_reviewed: mods.map((m) => m.key) } });
  } catch (e) {
    toast(errText(e), 'error');
    return false;
  }
  await afterSave(edits.length > 0);
  return true;
}

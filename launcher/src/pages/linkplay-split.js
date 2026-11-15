// Link Play: split screen, two players on this PC. The game runs twice, one window per player, and the second player visits the first.
// The choices are this page's own (remembered here, nothing goes to pkp.ini): Rust hands each window its role when it starts the pair
// (src-tauri/src/split.rs), and the players then meet in the game's own Link Play menu, which the two lines under the button name.
import { api } from '../api.js';
import { t } from '../i18n.js';
import { esc, group, icon, row, segmented, select, toggle } from '../ui.js';
import { store } from '../store.js';
import { gamePhase } from '../action.js';

const LS = 'pkp.split';

const stored = () => { try { return JSON.parse(localStorage.getItem(LS)) || {}; } catch { return {}; } };
const keep = (opt) => { try { localStorage.setItem(LS, JSON.stringify(opt)); } catch { /* private window: fine */ } };

const busy = (s) => s.starting || ['starting', 'running', 'finishing'].includes(gamePhase(s.game));
const canStart = (s) => s.app?.readiness?.kind === 'ready' && !busy(s);

/** Draws the section into `el` and keeps it current. */
export async function renderSplit(el, ctx) {
  const mem = stored();
  const opt = { side: !!mem.side, keyboard: !!mem.keyboard, sound: !!mem.sound, second: typeof mem.second === 'string' ? mem.second : '' };
  let players = { first: '', candidates: [] };

  // the profile remembered as player 2 may be gone, or be the active one now: then it is the first that can be, or a new one
  async function loadPlayers() {
    players = await api.splitPlayers().catch(() => players);
    if (!players.candidates.some((c) => c.id === opt.second)) opt.second = players.candidates[0]?.id ?? '';
  }

  const pathLine = (who, last) => `<div class="lp-path">
      <span class="lp-path-lead">${icon('gamepad-2')}${esc(who)}</span>
      ${['Link Play', t('Local connection'), last].map((p) => `<span class="chip on-art">${esc(p)}</span>`).join(icon('chevron-right'))}
    </div>`;

  function html() {
    const second = players.candidates.length
      ? select('split-second', players.candidates.map((c) => [c.id, c.name]), opt.second)
      : `<span class="faint">${esc(t('New profile: Player 2'))}</span>`;
    const pads = opt.keyboard
      ? t('Player 1 plays with the keyboard and mouse, player 2 with the controller.')
      : t('Player 1 plays with the first controller, player 2 with the second.');
    return `${group(t('Split screen'), [
        row(t('Screen'), segmented('split-layout', [['stacked', t('Top and bottom')], ['side', t('Side by side')]], opt.side ? 'side' : 'stacked', { label: t('Screen') })),
        row(t('Player 1 plays with'), segmented('split-input', [['pad', t('Controller')], ['kbm', t('Keyboard and mouse')]], opt.keyboard ? 'kbm' : 'pad', { label: t('Player 1 plays with') })),
        row(t('Player 2'), second),
        row(t("Sound from player 2's window"), toggle('split-sound', opt.sound)),
      ])}
      <div class="lp-split-go">
        <button class="btn primary" id="split-start" ${canStart(store.state) ? '' : 'disabled'}>${icon('play')}${esc(t('Start split screen'))}</button>
        <div class="lp-split-paths">
          ${pathLine(t('Player 1'), t('Invite Others to Visit'))}
          ${pathLine(t('Player 2'), t('Visit a Friend'))}
        </div>
      </div>
      <div class="banner info">${icon('info')}<div class="grow">${esc(t('The game runs twice, one window per player, and player 2 visits player 1.'))} ${esc(pads)}
        <div>${esc(t('A second player without a save starts from a copy of yours. Two games need about twice the memory: 32 GB is recommended.'))}</div>
      </div></div>`;
  }

  // rewritten only when it changed, and the focused control keeps the focus (the page's own rule, linkplay.js patch)
  function draw() {
    const next = html();
    if (el.dataset.html === next) return;
    const a = document.activeElement;
    const key = el.contains(a) ? (a.id ? `#${a.id}` : a.name ? `input[name="${a.name}"]:checked` : null) : null;
    el.innerHTML = next;
    el.dataset.html = next;
    if (key) el.querySelector(key)?.focus();
  }

  el.addEventListener('change', (e) => {
    const c = e.target;
    if (c.name === 'split-layout') opt.side = c.value === 'side';
    else if (c.name === 'split-input') opt.keyboard = c.value === 'kbm';
    else if (c.id === 'split-second') opt.second = c.value;
    else if (c.id === 'split-sound') opt.sound = c.checked;
    else return;
    keep(opt);
    draw();
  });
  el.addEventListener('click', (e) => {
    const b = e.target.closest('#split-start');
    if (b && !b.disabled) store.play({ split: { ...opt } });
  });

  // a start can make player 2's profile, and a game that ended frees the button: both show at the next change of phase
  let phase = gamePhase(store.state.game);
  ctx.on(async (s) => {
    const now = gamePhase(s.game);
    if (now !== phase) {
      phase = now;
      await loadPlayers();
    }
    draw();
  });

  await loadPlayers();
  draw();
}

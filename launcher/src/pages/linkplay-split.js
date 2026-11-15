// Link Play: split screen, two to four players on this PC. The game runs once per player, each in a window of their own, and the
// others visit player 1. The choices are this page's own (remembered here, nothing goes to pkp.ini): Rust hands each window its role
// when it starts the group (src-tauri/src/split.rs), and the players then meet in the game's own Link Play menu, which the two lines
// beside the button name.
import { api } from '../api.js';
import { t } from '../i18n.js';
import { esc, group, icon, row, segmented, select, toggle } from '../ui.js';
import { store } from '../store.js';
import { gamePhase } from '../action.js';

const LS = 'pkp.split';
const MAX_PLAYERS = 4;   // the game's own limit for one session (split.rs MAX_PLAYERS)

const stored = () => { try { return JSON.parse(localStorage.getItem(LS)) || {}; } catch { return {}; } };
const keep = (opt) => { try { localStorage.setItem(LS, JSON.stringify(opt)); } catch { /* private window: fine */ } };

const busy = (s) => s.starting || ['starting', 'running', 'finishing'].includes(gamePhase(s.game));
const canStart = (s) => s.app?.readiness?.kind === 'ready' && !busy(s);

/** Draws the section into `el` and keeps it current. */
export async function renderSplit(el, ctx) {
  const mem = stored();
  const opt = {
    players: [2, 3, 4].includes(mem.players) ? mem.players : 2,
    side: !!mem.side,
    keyboard: !!mem.keyboard,
    sound: !!mem.sound,
    others: Array.isArray(mem.others) ? mem.others.map((id) => (typeof id === 'string' ? id : '')) : [],   // players 2, 3, 4
  };
  let players = { first: '', candidates: [] };

  // A profile remembered for a player may be gone, be the active one now, or sit with another player: each player then gets the
  // first profile nobody has, and one for whom none is left gets a new profile at the start ('').
  function assign() {
    const taken = new Set();
    for (let i = 0; i < MAX_PLAYERS - 1; i++) {
      const known = (id) => players.candidates.some((c) => c.id === id) && !taken.has(id);
      if (!known(opt.others[i])) opt.others[i] = i < opt.players - 1 ? players.candidates.find((c) => !taken.has(c.id))?.id ?? '' : '';
      if (opt.others[i]) taken.add(opt.others[i]);
    }
  }
  async function loadPlayers() {
    players = await api.splitPlayers().catch(() => players);
    assign();
  }

  const pathLine = (who, last) => `<div class="lp-path">
      <span class="lp-path-lead">${icon('gamepad-2')}${esc(who)}</span>
      ${['Link Play', t('Local connection'), last].map((p) => `<span class="chip on-art">${esc(p)}</span>`).join(icon('chevron-right'))}
    </div>`;

  // one row per visiting player: a profile nobody else has; with none left, the start makes a new one
  function playerRow(n) {
    const mine = opt.others[n - 2];
    const elsewhere = new Set(opt.others.slice(0, opt.players - 1).filter((id, i) => id && i !== n - 2));
    const list = players.candidates.filter((c) => !elsewhere.has(c.id));
    const control = list.length
      ? select(`split-player-${n}`, list.map((c) => [c.id, c.name]), mine)
      : `<span class="faint">${esc(t('New profile'))}</span>`;
    return row(t('Player {n}', { n }), control);
  }

  function html() {
    const pads = opt.keyboard
      ? t('Player 1 plays with the keyboard and mouse, the others with the controllers in the order they were connected.')
      : t('Each player plays with one controller, in the order the controllers were connected.');
    // four windows are a quarter each: nothing to choose
    const screen = opt.players === 4 ? [] : [row(t('Screen'), segmented('split-layout', [['stacked', t('Top and bottom')], ['side', t('Side by side')]], opt.side ? 'side' : 'stacked', { label: t('Screen') }))];
    return `${group(t('Split screen'), [
        row(t('Players'), segmented('split-players', [2, 3, 4].map((n) => [n, String(n)]), opt.players, { label: t('Players') })),
        ...screen,
        row(t('Player 1 plays with'), segmented('split-input', [['pad', t('Controller')], ['kbm', t('Keyboard and mouse')]], opt.keyboard ? 'kbm' : 'pad', { label: t('Player 1 plays with') })),
        ...Array.from({ length: opt.players - 1 }, (_, i) => playerRow(i + 2)),
        row(t('Sound from the other windows'), toggle('split-sound', opt.sound)),
      ])}
      <div class="lp-split-go">
        <button class="btn primary" id="split-start" ${canStart(store.state) ? '' : 'disabled'}>${icon('play')}${esc(t('Start split screen'))}</button>
        <div class="lp-split-paths">
          ${pathLine(t('Player {n}', { n: 1 }), t('Invite Others to Visit'))}
          ${pathLine(t('The others'), t('Visit a Friend'))}
        </div>
      </div>
      <div class="banner info">${icon('info')}<div class="grow">${esc(t('The game runs once per player, each in a window of their own, and the others visit player 1.'))} ${esc(pads)}
        <div>${esc(t('A player without a save starts from a copy of yours. Each game needs about 7 GB of memory: 32 GB is recommended for two players, 64 GB for four.'))}</div>
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
    const player = /^split-player-(\d)$/.exec(c.id);
    if (c.name === 'split-players') opt.players = Number(c.value);
    else if (c.name === 'split-layout') opt.side = c.value === 'side';
    else if (c.name === 'split-input') opt.keyboard = c.value === 'kbm';
    else if (player) opt.others[player[1] - 2] = c.value;
    else if (c.id === 'split-sound') opt.sound = c.checked;
    else return;
    if (c.name === 'split-players') assign();   // a player who joins gets a profile, one who leaves frees theirs
    keep(opt);
    draw();
  });
  el.addEventListener('click', (e) => {
    const b = e.target.closest('#split-start');
    if (b && !b.disabled) store.play({ split: { ...opt, others: opt.others.slice(0, opt.players - 1) } });
  });

  // a start can make a player's profile, and a game that ended frees the button: both show at the next change of phase
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

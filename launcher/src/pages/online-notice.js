// The notice about online presence (docs/online-presence.md, recompia.com "Online presence (optional)"): what it does and what it never
// does, in plain words, so nobody switches it on without knowing and nobody fears what it is. Shown before it is switched on from any
// place in the launcher (the first-Play question, the Home chip, Settings > Game) and on request from Settings > Game.
import { t } from '../i18n.js';
import { confirm, esc, icon } from '../ui.js';

export const ONLINE_DOC_URL = 'https://github.com/Dittobyte/Recompia/blob/testing/docs/online-presence.md';

function noticeHtml() {
  const list = (cls, ic, items) => `<ul class="modal-list">${items.map((x) => `<li class="${cls}">${icon(ic)}<span>${esc(x)}</span></li>`).join('')}</ul>`;
  return `
    <p><strong>${esc(t('What it does'))}</strong></p>
    ${list('ok', 'circle-check', [
      t('Only while the game runs and this switch is on, the launcher tells the Recompia online server that you are playing: once at the start, every 30 seconds, and once when the game closes.'),
      t('It sends your online name (empty: anonymous), the Recompia version, Windows or Linux, how many Pokemon have a home in your save (one number, counted on your PC) and a random online ID made on your PC.'),
      t('Anyone can see who is online and the leaderboard. Leave the online name empty to stay anonymous.'),
    ])}
    <p><strong>${esc(t('What it never does'))}</strong></p>
    ${list('bad', 'circle-x', [
      t('It never sends your save, your profile nickname, your user ID, your PC name, your files or your position in the game.'),
      t('No cookies, no ads, no analytics, no tracking. Nothing is sold or given to anyone.'),
      t('The server cannot control your PC: nothing is downloaded or run from it. Gameplay, Link Play and your saves stay exactly the same.'),
      t('When it is off, nothing is sent at all.'),
    ])}
    <p><strong>${esc(t('How to stop it'))}</strong></p>
    ${list('', 'info', [
      t("Switch it off on Home, in Settings > Game or on the game's Online tab: you leave the list within a few seconds."),
      t('Sessions are deleted 24 hours after you go offline. To have your rows removed, ask on the Recompia Discord with your online name.'),
      t('The full explanation is docs/online-presence.md in the Recompia repository on GitHub (Settings > Game > Online, Full explanation).'),
    ])}`;
}

// wide: the notice is three short lists; at the default width it would need scrolling on a handheld
const wide = (dlg) => dlg.classList.add('wide');

/** Before it is switched on: the notice, then "Yes, share it" or "No, thanks". No is focused; Esc and a click outside are no too. */
export const askTurnOn = () =>
  confirm({ title: t('Share that you are online?'), html: noticeHtml(), ok: t('Yes, share it'), cancel: t('No, thanks'), focusCancel: true, setup: wide });

/** The notice on its own (Settings > Game > What is and is not sent). */
export const showOnlineNotice = () => confirm({ title: t('Online presence'), html: noticeHtml(), ok: t('Close'), alert: true, setup: wide });

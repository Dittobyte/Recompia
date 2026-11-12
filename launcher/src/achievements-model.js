// The Achievements page, the pure part: the hub's list grouped as the hub orders it, the percentages and the rarest ones, what the
// player has earned, and what the Community Achievements mod's state means for the page. No DOM and no Tauri, so
// `node scripts/test-achievements-model.mjs` runs it. The page is pages/achievements.js; the list comes from Rust (achievements.rs
// AchievementsView), the mod's state from the Mods page's list (mods.rs ModsView).
import { t } from './i18n.js';

/** The title of a group of the hub's list (its `cat`). The hub names no titles, so these are ours; an unknown group shows its id. */
export function categoryTitle(cat) {
  switch (cat) {
    case 'pokedex': return t('Pokédex');
    case 'habitats': return t('Habitats');
    case 'items': return t('Items');
    case 'recipes': return t('Recipes');
    case 'emotes': return t('Emotes');
    case 'town': return t('Towns');
    case 'streak': return t('Days in a row');
    case 'dishes': return t('Dishes');
    case 'relics': return t('Relics');
    case 'photos': return t('Photos');
    case 'buildings': return t('Buildings');
    default: return cat ? cat.charAt(0).toUpperCase() + cat.slice(1) : t('Other');
  }
}

/** The list as groups, in the order each group first appears in the hub's list: [{ cat, title, items }]. */
export function groupList(list) {
  const groups = [];
  for (const item of list) {
    let g = groups.find((x) => x.cat === item.cat);
    if (!g) groups.push((g = { cat: item.cat, title: categoryTitle(item.cat), items: [] }));
    g.items.push(item);
  }
  return groups;
}

/** How many of the list the player has earned (an id in `earned` that the list does not have is not counted). */
export const earnedCount = (list, earned) => list.filter((a) => Object.hasOwn(earned, a.id)).length;

/** The share of the players who have an achievement, as the mod words it: "12%", "under 1%", "0%"; '' when the counts are not known. */
export function percentText(count, players) {
  if (count == null || !(players > 0)) return '';
  if (count <= 0) return '0%';
  const pct = (count / players) * 100;
  return pct < 1 ? t('under 1%') : `${Math.min(100, Math.round(pct))}%`;
}

/** The players' side of the summary: the number sharing and the achievements earned in total; null when the counts are not known. */
export function totals(view) {
  if (!view.stats) return null;
  return { players: view.stats.players, earned: view.list.reduce((n, a) => n + (a.players ?? 0), 0) };
}

/** The rarest right now: up to `n` achievements somebody has earned, fewest players first (the list's order breaks a tie). */
export function rarest(list, n = 3) {
  return list
    .map((item, i) => ({ item, i }))
    .filter(({ item }) => item.players > 0)
    .sort((a, b) => a.item.players - b.item.players || a.i - b.i)
    .slice(0, n)
    .map(({ item }) => item);
}

/** The date an achievement was earned, from the unix time the mod keeps. */
export const dateText = (unix, locale) => new Date(unix * 1000).toLocaleDateString(locale, { year: 'numeric', month: 'short', day: 'numeric' });

/** What the mod's state is, from the Mods page's list: { kind, mod }. kind: 'unknown' (the list could not be read), 'missing' (not
 *  installed), 'broken', 'off' (installed, switched off), 'private' (on, sharing off) or 'sharing' (on, sharing on). */
export function modState(modsView, modId) {
  if (!modsView) return { kind: 'unknown', mod: null };
  const mod = modsView.mods.find((m) => m.id === modId);
  if (!mod) return { kind: 'missing', mod: null };
  if (mod.state === 'error') return { kind: 'broken', mod };
  if (!mod.enabled) return { kind: 'off', mod };
  const share = mod.settings.find((s) => s.key === 'share');
  return { kind: share && share.number === 0 ? 'private' : 'sharing', mod };
}

/** The one sentence for the mod's state. */
export function modLine(state) {
  switch (state.kind) {
    case 'missing': return t('The Community Achievements mod is not installed, so nothing is earned yet. Get it from the mod hub and put it into the mods folder.');
    case 'broken': return t('The Community Achievements mod has a problem and does not run.');
    case 'off': return t('The Community Achievements mod is installed but switched off, so nothing is earned.');
    case 'private': return t('The Community Achievements mod is on. Sharing is off: your achievements stay on this PC and you are not counted.');
    case 'sharing': return t('The Community Achievements mod is on and sharing: you are counted with the other players.');
    default: return t('The mods could not be read, so the mod\'s state is not known.');
  }
}

/** The buttons the state offers, in order: 'get' (the mod's hub page), 'folder' (the mods folder), 'enable' (switch it on), 'share'
 *  (switch sharing on). */
export function modActions(state) {
  switch (state.kind) {
    case 'missing': return ['get', 'folder'];
    case 'broken': return ['folder'];
    case 'off': return ['enable'];
    case 'private': return ['share'];
    default: return [];
  }
}

/** What sharing sends, for the page (the mod's own words in its design notes: Privacy). */
export const sharingText = () =>
  t('Sharing sends the list of the achievements you have earned and the game version, under a random number made on your PC. Never your name, your save or anything else from your PC. Only the mod sends it; the launcher sends nothing.');

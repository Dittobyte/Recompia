// The handheld report: the few lines of pkp.log a bug report about an AMD handheld needs, with names, home paths, addresses and room codes
// removed, as one block of text. Pure (no DOM, no storage, no Tauri): scripts/test-handheld-report.mjs runs it with node. The log text itself
// comes from testing_log_read, which already hides user names and IP addresses; scrub() here is a second pass, so the report is safe even if
// that one changes.

export const MAX_LINES = 120;
export const MAX_LINE_CHARS = 700;   // the runtime cuts a message at 2000; "host facts:" and the footprint readings are about 460
const MAX_FAULTS = 40;
const MAX_IDENTITY = 40;
const KEEP_PERIODIC = [15, 25];      // the first and the last of the repetitive lines (footprint, "N frames in")

export const TRUNCATED_NOTE = 'NOTE: the start of the log was not read (long session): restart the game, play two minutes and copy again so the device lines are included';

// The wording is the runtime's own (host_device.cpp, vk_device.cpp, nvn_pool_copy.cpp, memguard.cpp, time.cpp, services/common.cpp,
// diag/gauge.cpp, diag/crash.cpp, core/runtime.cpp). FAULTS are always kept, IDENTITY lines say what the device is, PERIODIC lines repeat
// all session long and are thinned, the rest fill what is left.
export const FAULTS = /device lost|VK_ERROR|HOST EXCEPTION|uncaught|waited about .* on the GPU|no frame presented|pool_copy.*over the cap|memory limit|\[memory\] limit_gb|\bcrash(?!ed)|\bfatal\b/i;
export const IDENTITY = /\bhost: |\bhandheld\b|\[host\]|\bGPU \d+:|using GPU|memory heaps|nxrt starting|short sleeps|operation mode|pool_copy (?:on|off)/i;
export const PERIODIC = /\bfootprint\b|\bframes in\b/i;
export const OTHER = /\bVulkan\b.*driver|\bdriver 0x|pool_copy/i;

const ROOM = /\b[0-9A-Z*]{4}(?:-[0-9A-Z*]{4}){3}\b/gi;
const IPV4 = /(?<![\w.])(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)(?![\w.]*\w)(?::\d{1,5})?/g;
const IPV6 = /(?<![\w:])[0-9a-f]{0,4}(?::[0-9a-f]{0,4}){2,7}(?![\w:])/gi;

const isIpv6 = (s) => {
  if (!/[0-9a-f]/i.test(s) || /^\d{1,2}:\d{2}:\d{2}$/.test(s)) return false;
  const groups = s.split(':');
  return s.includes('::') ? groups.length <= 8 : groups.length === 8;
};

/** One log line with everything personal replaced by a marker (<user>, <ip>, <room>, <path>); the words around it stay. */
export function scrub(line) {
  let s = String(line ?? '');
  s = s.replace(/(?<![:\w])(?:\\\\|\/\/)[^\\/\s'"]+[\\/][^\\/\s'"]+(?:[\\/][^\s'"]*)?/g, '<path>');   // \\NAS\share, //NAS/share
  s = s.replace(/\b[A-Za-z]:[\\/](?:[^\\/:*?"|\r\n]+[\\/])*([^\\/:*?"|\s'",;)]*)/g, (_, file) => (file ? `<path>\\${file}` : '<path>\\'));   // any drive path, either slash; the file name stays
  // Unix homes and mounts: the name may hold spaces, so cut to the end of the path token (no later slash: to the end of the line)
  s = s.replace(/(?<![\w.])\/(?:home|Users|run\/media|mnt|media)\/(?:[^/\r\n]*\/[^\s'"]*|[^\r\n]*)/g, '<path>');
  s = s.replace(ROOM, '<room>');
  s = s.replace(/\broom[ _]code\b\s*[=:]?\s*\S+/gi, 'room code <room>');
  s = s.replace(IPV4, '<ip>');
  s = s.replace(IPV6, (m) => (isIpv6(m) ? '<ip>' : m));
  s = s.replace(/\b(uid)\s*=.*$/i, '$1 = <removed>');
  s = s.replace(/\b(nickname)\b.*$/i, '$1 <removed>');
  return s;
}

const clip = (s) => (s.length > MAX_LINE_CHARS ? `${s.slice(0, MAX_LINE_CHARS - 3)}...` : s);

const PREFIX = /^\s*\[[^\]]*\]\s*/;   // "[  0.004521 I t1234 main] ": time, level, thread; the tag and the message stay
const thin = (list, max) => (list.length <= max ? list : [...list.slice(0, Math.ceil(max / 2)), ...list.slice(list.length - Math.floor(max / 2))]);
const thinEnds = (list, [first, last]) => (list.length <= first + last ? list : [...list.slice(0, first), ...list.slice(list.length - last)]);

/**
 * The lines that matter: { total, lines, left_out, truncated }. Only lines of a topic are kept, with the log prefix removed, scrubbed and
 * cut (after the prefix is gone). Faults always stay (up to 40), identity lines too (up to 40), the repetitive lines are thinned, repeats
 * are kept once, and at most MAX_LINES come out, in the order of the log. `truncated` is the viewer's flag: the start was not read.
 */
export function extractReport(logText, truncated = false) {
  const seen = new Set();
  const faults = [];
  const identity = [];
  const periodic = [];
  const other = [];
  const all = String(logText ?? '').split(/\r\n|\r|\n/);
  all.forEach((raw, i) => {
    if (!raw.trim()) return;
    const kind = FAULTS.test(raw) ? faults : IDENTITY.test(raw) ? identity : PERIODIC.test(raw) ? periodic : OTHER.test(raw) ? other : null;
    if (!kind) return;
    const line = clip(scrub(raw.replace(PREFIX, '')).trim());
    if (seen.has(line)) return;
    seen.add(line);
    kind.push({ i, line });
  });
  const total = faults.length + identity.length + periodic.length + other.length;
  const chosen = [...thin(faults, MAX_FAULTS), ...thin(identity, MAX_IDENTITY)];
  const periodicKept = thinEnds(periodic, KEEP_PERIODIC);
  const room = Math.max(0, MAX_LINES - chosen.length - periodicKept.length);
  const picked = [...chosen, ...periodicKept, ...other.slice(0, room)].sort((a, b) => a.i - b.i);
  return { total: all.length, lines: picked.map((x) => x.line), left_out: total - picked.length, truncated: !!truncated };
}

/** The text block to paste into a bug report. `date` is for tests; it defaults to today. */
export function formatReport(report, { version = '', channel = '', date = new Date() } = {}) {
  const r = report ?? { lines: [], left_out: 0 };
  const note = r.truncated ? [TRUNCATED_NOTE, ''] : [];
  const day = date.toISOString().slice(0, 10);
  const body = r.lines.length ? r.lines.join('\n') : '(no matching lines in pkp.log: start the game once and try again)';
  const gap = r.left_out ? `\n(${r.left_out} more matching lines left out)` : '';
  return [
    `Handheld report, launcher ${version || 'unknown'}, channel ${channel || 'unknown'}, ${day}`,
    'Names, home folders, addresses and room codes were removed.',
    '',
    ...note,
    body + gap,
    '',
    'Please add: your device, your OS, the power mode (battery or plugged in) and what you were doing.',
  ].join('\n');
}

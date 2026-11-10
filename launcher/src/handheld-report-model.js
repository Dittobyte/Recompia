// The handheld report: the few lines of pkp.log a bug report about an AMD handheld needs, with names, home paths, addresses and room codes
// removed, as one block of text. Pure (no DOM, no storage, no Tauri): scripts/test-handheld-report.mjs runs it with node. The log text itself
// comes from testing_log_read, which already hides user names and IP addresses; scrub() here is a second pass, so the report is safe even if
// that one changes.

export const MAX_LINES = 120;
export const MAX_LINE_CHARS = 240;

// The wording is the runtime's own (host_device.cpp, vk_device.cpp, nvn_pool_copy.cpp, memguard.cpp, time.cpp, services/common.cpp,
// pkp_play.cpp, diag/gauge.cpp, diag/crash.cpp). Each row: a name for the group and the pattern that finds its lines.
export const TOPICS = [
  ['host', /\bhandheld\b|\[host\]|\bhost: /i],
  ['gpu', /\bGPU \d+:|using GPU|\bVulkan\b.*driver|\bdriver 0x/i],
  ['memory', /memory heaps|graphics pools|graphics arena|footprint|memory limit|\[memory\]|MEMORY LIMIT/i],
  ['pool_copy', /pool_copy/i],
  ['timing', /short sleeps|operation mode/i],
  ['picture', /\baspect\b|render resolution|scene resolution/i],
  ['frames', /\bframes in\b|waited about .* on the GPU|no frame presented/i],
  ['faults', /device lost|HOST EXCEPTION|uncaught|\bcrash|\bfatal\b/i],
];

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
  s = s.replace(/\b([A-Za-z]:\\(?:Users|home)\\)[^\\/\s'"]+/gi, '$1<user>');   // C:\Users\x, Z:\home\x
  s = s.replace(/(\/(?:home|Users)\/)[^\\/\s'"]+/g, '$1<user>');                 // /home/x, /Users/x
  s = s.replace(/\\\\[^\\\s'"]+\\[^\\\s'"]+/g, '<path>');                         // \\server\share
  s = s.replace(/\b[A-Za-z]:\\(?:[^\\/:*?"|\r\n]+\\)*([^\\/:*?"|\s'",;)]*)/g, (_, file) => (file ? `<path>\\${file}` : '<path>\\'));   // drive paths keep the file name
  s = s.replace(ROOM, '<room>');
  s = s.replace(/\broom[ _]code\b\s*[=:]?\s*\S+/gi, 'room code <room>');
  s = s.replace(IPV4, '<ip>');
  s = s.replace(IPV6, (m) => (isIpv6(m) ? '<ip>' : m));
  s = s.replace(/\b(uid)\s*=.*$/i, '$1 = <removed>');
  s = s.replace(/\b(nickname)\b.*$/i, '$1 <removed>');
  return s;
}

const clip = (s) => (s.length > MAX_LINE_CHARS ? `${s.slice(0, MAX_LINE_CHARS - 3)}...` : s);

/**
 * The lines that matter, grouped: { total, lines, left_out }. Only lines that match a topic are kept (no plain start or end of the log), each
 * scrubbed and trimmed, repeated lines once, at most MAX_LINES (the first half and the last half when there are more).
 */
export function extractReport(logText) {
  const seen = new Set();
  const kept = [];
  const all = String(logText ?? '').split(/\r\n|\r|\n/);
  for (const raw of all) {
    if (!raw.trim() || !TOPICS.some(([, re]) => re.test(raw))) continue;
    const line = clip(scrub(raw).trim());
    if (seen.has(line)) continue;
    seen.add(line);
    kept.push(line);
  }
  if (kept.length <= MAX_LINES) return { total: all.length, lines: kept, left_out: 0 };
  const half = MAX_LINES / 2;
  return { total: all.length, lines: [...kept.slice(0, half), ...kept.slice(kept.length - half)], left_out: kept.length - MAX_LINES };
}

/** The text block to paste into a bug report. `date` is for tests; it defaults to today. */
export function formatReport(report, { version = '', channel = '', date = new Date() } = {}) {
  const r = report ?? { lines: [], left_out: 0 };
  const day = date.toISOString().slice(0, 10);
  const body = r.lines.length ? r.lines.join('\n') : '(no matching lines in pkp.log: start the game once and try again)';
  const gap = r.left_out ? `\n(${r.left_out} more matching lines left out)` : '';
  return [
    `Handheld report, launcher ${version || 'unknown'}, channel ${channel || 'unknown'}, ${day}`,
    'Names, home folders, addresses and room codes were removed.',
    '',
    body + gap,
    '',
    'Please add: your device, your OS, the power mode (battery or plugged in) and what you were doing.',
  ].join('\n');
}

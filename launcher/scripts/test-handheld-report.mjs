// Run: node scripts/test-handheld-report.mjs (no browser, no Tauri). The handheld report: what it keeps, what it removes.
import assert from 'node:assert/strict';
import { MAX_LINES, MAX_LINE_CHARS, extractReport, formatReport, scrub } from '../src/handheld-report-model.js';

let n = 0;
const ok = (cond, msg) => { assert.ok(cond, msg); n += 1; };
const eq = (a, b, msg) => { assert.equal(a, b, msg); n += 1; };
const has = (s, part, msg) => ok(s.includes(part), `${msg ?? part} in: ${s}`);
const lacks = (s, part, msg) => ok(!s.includes(part), `${msg ?? part} not in: ${s}`);

// scrub: every rule
let s = scrub('opened C:\\Users\\Alice\\Documents\\pkp\\pkp.log');
lacks(s, 'Alice'); has(s, '<path>\\pkp.log', 'windows home path becomes a marker');
s = scrub('loaded /home/deck/.local/share/pkp/shaders');
lacks(s, 'deck'); has(s, '/home/<user>/', 'linux home');
s = scrub('path /Users/bob/Library/pkp');
lacks(s, 'bob'); has(s, '/Users/<user>/', 'mac home');
s = scrub('Z:\\home\\steam\\game\\pkp.exe');
lacks(s, 'steam'); has(s, '<path>\\pkp.exe', 'wine home');
s = scrub('shader cache D:\\Games\\Pokopia\\shader_cache\\a.bin was read');
lacks(s, 'Games'); lacks(s, 'D:\\Games'); has(s, '<path>\\a.bin', 'drive path keeps the file name'); has(s, 'was read', 'words stay');
s = scrub('connect to 192.168.1.20:7777 now');
lacks(s, '192.168'); has(s, '<ip>', 'ipv4'); has(s, 'connect to', 'word kept');
s = scrub('peer fe80::1ff:fe23:4567:890a and 2001:db8::1 seen');
lacks(s, 'fe80'); lacks(s, '2001:db8'); has(s, 'peer', 'ipv6 word kept');
s = scrub('ldn: room code ABCD-EF12-3456-GHJK hosted');
lacks(s, 'ABCD-EF12'); has(s, '<room>', 'room code');
s = scrub('ldn: room code ABCD-****-****-**** here');
lacks(s, 'ABCD'); has(s, '<room>', 'masked room code');
s = scrub('[net] uid = 12345-abcdef extra');
lacks(s, '12345'); has(s, 'uid = <removed>', 'uid');
s = scrub('Link Play host x (nickname Fluffy the Great)');
lacks(s, 'Fluffy'); has(s, 'nickname <removed>', 'nickname');

// things that must stay
eq(scrub('Vulkan 1.3.280, driver 0x8000b00'), 'Vulkan 1.3.280, driver 0x8000b00', 'versions stay');
eq(scrub('[12:34:56.789] 144 frames in 2.0 s'), '[12:34:56.789] 144 frames in 2.0 s', 'times stay');
eq(scrub(''), '', 'empty line');
eq(scrub(null), '', 'null line');

// extractReport
const LOG = [
  '[00:00:01.000] first line of the log with C:\\Users\\Alice\\secret',
  '[00:00:01.100] host: handheld on (board Jupiter)',
  '[00:00:01.200] GPU 0: AMD Radeon 780M (integrated, Vulkan 1.3.280, driver 0x8000b00, 8.0 GiB)',
  '[00:00:01.300] vk: memory heaps 0:8192MiB; types 0:1; maxBufferSize 4096',
  '[00:00:01.400] pool_copy on (auto)',
  '[00:00:01.500] short sleeps (up to 200 us): yielded through',
  '[00:00:01.600] operation mode: handheld ([am] operation_mode = auto)',
  '[00:00:01.700] aspect 1.7778 (window 1280x800): 1920x1080 targets at scale 0.667',
  '[00:00:02.000] unrelated chatter about audio',
  '[00:00:03.000] 600 frames in 10.0 s: mean 60.0 fps, 1% low 30.0 fps, worst 40.0 ms',
  '[00:00:04.000] graphics device lost in submit (VK_ERROR_DEVICE_LOST); stopping further GPU work',
  '[00:00:05.000] uncaught C++ exception at /home/deck/x',
].join('\r\n');
const r = extractReport(LOG);
eq(r.lines.length, 10, 'only the matching lines (CRLF input)');
ok(r.lines.every((l) => !l.includes('\r')), 'no CR left');
ok(!r.lines.some((l) => l.includes('unrelated') || l.includes('first line')), 'other lines are not copied');
ok(!r.lines.join('\n').match(/deck\b.*home|\/home\/deck/), 'user removed from kept lines');
eq(extractReport('').lines.length, 0, 'empty log');
eq(extractReport(undefined).lines.length, 0, 'no log');
eq(extractReport('a\nb\nc').lines.length, 0, 'nothing matches');

// a huge log: capped, long lines trimmed, repeats once
const big = Array.from({ length: 50000 }, (_, i) => `[${i}] ${i % 2 ? 'frames in ' : 'device lost '}${i} ${'x'.repeat(500)}`).join('\n');
const rb = extractReport(big);
ok(rb.lines.length <= MAX_LINES, 'at most 120 lines');
ok(rb.left_out > 0, 'says lines were left out');
ok(rb.lines.every((l) => l.length <= MAX_LINE_CHARS), 'lines trimmed to 240');
eq(extractReport('device lost\ndevice lost\ndevice lost').lines.length, 1, 'repeated line once');

// a user name inside a path, end to end
const pathLog = 'vk: crash dump at C:\\Users\\Alice Smith\\AppData\\pkp_crash.txt and /home/alice/pkp\nhost: handheld on 10.0.0.5';
const txt = formatReport(extractReport(pathLog), { version: '0.9.1', channel: 'testing', date: new Date('2026-11-10T12:00:00Z') });
lacks(txt, 'Alice'); lacks(txt, 'alice'); lacks(txt, '10.0.0.5');

// formatReport
has(txt, 'launcher 0.9.1', 'version'); has(txt, 'channel testing', 'channel'); has(txt, '2026-11-10', 'date');
has(formatReport(extractReport(''), { version: '1', channel: 'testing' }), 'no matching lines', 'empty body says so');
has(txt, 'your device', 'footer: device'); has(txt, 'your OS', 'footer: OS'); has(txt, 'power mode', 'footer: power'); has(txt, 'what you were doing', 'footer: activity');
has(formatReport({ lines: ['a'], left_out: 7 }), '7 more matching lines', 'left-out note');
has(formatReport(null), 'unknown', 'no arguments');

assert.ok(n >= 15);
console.log(`handheld report: ${n} checks passed`);

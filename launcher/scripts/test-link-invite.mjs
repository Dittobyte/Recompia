// Run: node scripts/test-link-invite.mjs (no browser, no Tauri). The Link Play invite: make, parse, and the settings it writes.
import assert from 'node:assert/strict';
import { addrProblem, applyEdits, editsFor, readIni, tailscaleAddr, uiFrom } from '../src/linkplay-model.js';
import { addrKind, inviteNote, inviteToUi, makeInvite, normalizeCode, parseInvite } from '../src/link-invite-model.js';

const CODE = 'ABCD-EFGH-JKMN-PQRS';

// codes: case, dashes, spaces, O/0 and I/L/1 are one code
assert.equal(normalizeCode('abcd efgh jkmn pqrs'), CODE);
assert.equal(normalizeCode('ABCDEFGHJKMNPQRS'), CODE);
assert.equal(normalizeCode('O1l1-0000-0000-0000'), '0111-0000-0000-0000');
assert.equal(normalizeCode('ABCD-EFGH-JKMN'), null);
assert.equal(normalizeCode('ABCD-EFGH-JKMN-PQRSA'), null);
assert.equal(normalizeCode('ABCD-EFGH-JKMN-PQRU'), null, 'U is not in the alphabet');

// kinds
assert.equal(addrKind('192.168.1.20'), 'lan');
assert.equal(addrKind('10.0.0.5:47600'), 'lan');
assert.equal(addrKind('169.254.3.4'), 'lan');
assert.equal(addrKind('100.101.102.103'), 'tailscale');
assert.equal(addrKind('100.63.0.1'), 'internet');
assert.equal(addrKind('100.128.0.1'), 'internet');
assert.equal(addrKind('203.0.113.5'), 'internet');
assert.ok(tailscaleAddr('100.64.0.1') && tailscaleAddr('100.127.255.255') && !tailscaleAddr('100.128.0.0'));

// make, then parse: every mode comes back as it went in
const cases = [
  [{ addr: '192.168.1.20' }, { kind: 'lan', addr: '192.168.1.20:47600', code: '' }],
  [{ addr: '192.168.1.20:47601', code: CODE }, { kind: 'lan', addr: '192.168.1.20:47601', code: '' }],   // a LAN invite carries no code
  [{ addr: '100.101.102.103', code: CODE }, { kind: 'tailscale', addr: '100.101.102.103:47600', code: CODE }],
  [{ addr: '203.0.113.5', code: 'abcdefghjkmnpqrs' }, { kind: 'internet', addr: '203.0.113.5:47600', code: CODE }],
  [{ addr: '203.0.113.5', code: CODE, through: true }, { kind: 'relay', addr: '203.0.113.5:47610', code: CODE, relay: '203.0.113.5:47610' }],
  [{ addr: '100.101.102.103:47610', code: CODE, through: true }, { kind: 'relay', addr: '100.101.102.103:47610', code: CODE, relay: '100.101.102.103:47610' }],
];
for (const [input, want] of cases) {
  const made = makeInvite(input);
  assert.ok(made, JSON.stringify(input));
  for (const shape of [made.text, made.link, `  ${made.text.replaceAll('\n', '\r\n')}  `, `hey, here you go!\n\n${made.text}\nsee you there`]) {
    const got = parseInvite(shape);
    assert.equal(got.ok, true, `${JSON.stringify(input)} -> ${shape}`);
    for (const [k, v] of Object.entries(want)) assert.equal(got.invite[k], v, `${k} of ${JSON.stringify(input)}`);
  }
}

// not made: no code for a non-LAN address or for a relay (even on a LAN address), own PC, bad address
assert.equal(makeInvite({ addr: '192.168.1.20:47610', through: true }), null);
assert.equal(makeInvite({ addr: '100.101.102.103' }), null);
assert.equal(makeInvite({ addr: '127.0.0.1', code: CODE }), null);
assert.equal(makeInvite({ addr: 'nonsense', code: CODE }), null);
assert.equal(makeInvite({ addr: '203.0.113.5', code: 'short' }), null);

// parsed from less: "address code" and labelled lines, no link
let r = parseInvite(`100.101.102.103 ${CODE.toLowerCase()}`);
assert.ok(r.ok && r.invite.kind === 'tailscale' && r.invite.code === CODE);
r = parseInvite(`Join over Tailscale: 100.101.102.103:47600\nRoom code: ${CODE}`);
assert.ok(r.ok && r.invite.addr === '100.101.102.103:47600');
r = parseInvite('192.168.1.20');
assert.ok(r.ok && r.invite.kind === 'lan' && r.invite.code === '');

// a code followed by words on the same line is still the code
r = parseInvite(`Join: 100.101.102.103:47600\nRoom code: ${CODE} thanks!`);
assert.ok(r.ok && r.invite.code === CODE, 'trailing words after the code');

// refused, in words
for (const [bad, part] of [
  ['', 'Paste'],
  ['hello there', 'does not look like an invite'],
  ['pokopia-link://join?v=2&a=192.168.1.20', 'newer launcher'],
  ['pokopia-link://join?v=1&a=127.0.0.1', 'your own PC'],
  ['pokopia-link://join?v=1&a=999.1.1.1', 'not an IPv4'],
  ['pokopia-link://join?v=1&a=203.0.113.5', 'no room code'],
  [`pokopia-link://join?v=1&a=203.0.113.5&c=ABC`, 'not valid'],
]) {
  r = parseInvite(bad);
  assert.equal(r.ok, false, bad);
  assert.ok(r.error.includes(part), `${bad}: ${r.error}`);
}

// the page: Tailscale is explained, not just refused
assert.match(addrProblem('100.101.102.103', { lan: true, remote: true }), /Tailscale address/);
assert.match(addrProblem('203.0.113.5', { lan: true, remote: true }), /Not on your network/);

// what an invite writes: exactly the edits the page makes by hand, nothing outside [net]
const base = { 'net.protocol': 'native', 'net.mode': 'auto', 'net.server': '127.0.0.1:39660' };
const write = (invite) => {
  const ui = { ...uiFrom(base, {}), ...inviteToUi(invite) };
  const edits = editsFor(ui, base);
  assert.ok(edits?.length, 'an invite changes the file');
  for (const e of edits) assert.ok(e.file === 'pkp' && e.section === 'net', `${e.section}.${e.key} is outside [net]`);
  assert.ok(!edits.some((e) => e.key === 'server'), 'the old [net] server is never written');
  return readIni(applyEdits(base, edits));
};
let n = write(parseInvite(makeInvite({ addr: '192.168.1.20' }).text).invite);
assert.deepEqual([n.mode, n.internet, n.relay, n.code], ['join:192.168.1.20:47600', false, '', '']);
n = write(parseInvite(makeInvite({ addr: '100.101.102.103', code: CODE }).text).invite);
assert.deepEqual([n.mode, n.internet, n.relay, n.code], ['join:100.101.102.103:47600', true, '', CODE]);
n = write(parseInvite(makeInvite({ addr: '203.0.113.5', code: CODE, through: true }).text).invite);
assert.deepEqual([n.mode, n.internet, n.relay, n.code], ['auto', true, '203.0.113.5:47610', CODE]);
n = write(parseInvite(makeInvite({ addr: '203.0.113.5', code: CODE }).text).invite);
assert.deepEqual([n.mode, n.internet, n.relay, n.code], ['join:203.0.113.5:47600', true, '', CODE]);

// the note names what happened
assert.match(inviteNote(parseInvite(makeInvite({ addr: '100.101.102.103', code: CODE }).text).invite), /tailscale/i);
console.log('link invite ok');

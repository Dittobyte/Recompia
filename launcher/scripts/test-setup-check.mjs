// Run: node scripts/test-setup-check.mjs (no browser, no Tauri). The Link Play setup check: facts to plain findings, probe results to messages.
import assert from 'node:assert/strict';
import { probeMessage, setupFindings } from '../src/setup-check-model.js';

const LAN = { lan: [{ ip: '192.0.2.42', adapter: 'Ethernet', default_route: true, public_profile: false }], overlay: [], skipped: [], warning: null, firewall_hint: null };
const fw = (state, zones, ports) => ({ firewalld: { state, zones, ports } });
const get = (fs, id) => fs.find((f) => f.id === id);
const levels = (fs) => fs.map((f) => f.level);

// strict zone (FedoraServer, no ports): a problem that names the port and shows the sudo command as text
let fs = setupFindings({ lan: LAN, facts: fw('running', ['FedoraServer'], []) });
let f = get(fs, 'firewall-blocks');
assert.equal(f.level, 'problem');
assert.match(f.title, /47600/);
assert.match(f.detail, /FedoraServer/);
assert.ok(f.fix.includes('sudo firewall-cmd --add-port=47600/tcp --add-port=47600/udp'));
assert.ok(!get(fs, 'firewall-open'));

// workstation zone with 1025-65535/tcp and /udp: ok
fs = setupFindings({ lan: LAN, facts: fw('running', ['FedoraWorkstation'], ['1025-65535/tcp', '1025-65535/udp']) });
assert.equal(get(fs, 'firewall-open').level, 'ok');
assert.match(get(fs, 'firewall-open').title, /allows port 47600/);
assert.ok(!get(fs, 'firewall-blocks'));

// exact ports
fs = setupFindings({ lan: LAN, facts: fw('running', ['public'], ['47600/tcp', '47600/udp']) });
assert.equal(get(fs, 'firewall-open').level, 'ok');

// only UDP open is still a problem; so is a range that misses the port
fs = setupFindings({ lan: LAN, facts: fw('running', ['public'], ['47600/udp']) });
assert.equal(get(fs, 'firewall-blocks').level, 'problem');
fs = setupFindings({ lan: LAN, facts: fw('running', ['public'], ['1-1024/tcp', '1-1024/udp']) });
assert.equal(get(fs, 'firewall-blocks').level, 'problem');
fs = setupFindings({ lan: LAN, facts: fw('running', ['public'], ['1025-65535/tcp', '1025-65535/udp', 'junk']), port: 40000 });
assert.equal(get(fs, 'firewall-open').level, 'ok');

// firewall switched off: tip; absent: nothing about the firewall
fs = setupFindings({ lan: LAN, facts: fw('not_running', [], []) });
assert.equal(get(fs, 'firewall-off').level, 'tip');
fs = setupFindings({ lan: LAN, facts: { firewalld: { state: 'absent', zones: [], ports: [] } } });
assert.ok(!fs.some((x) => x.id.startsWith('firewall')), 'state: \'absent\' says nothing about the firewall');
fs = setupFindings({ lan: LAN, facts: null });
assert.ok(!fs.some((x) => x.id.startsWith('firewall')));

// no address
fs = setupFindings({ lan: null, facts: null });
assert.equal(fs[0].id, 'no-address');
assert.equal(fs[0].level, 'problem');
assert.match(fs[0].title, /no network address/);
assert.equal(setupFindings({ lan: { lan: [], overlay: [] }, facts: null })[0].id, 'no-address');

// Public network comes from lan.warning
const warn = 'Windows treats this network as Public, so other PCs may not reach you.';
fs = setupFindings({ lan: { ...LAN, warning: warn, firewall_hint: 'Allow it on Private networks.' }, facts: null });
f = get(fs, 'public-network');
assert.equal(f.level, 'problem');
assert.match(f.title, /Public network/);
assert.equal(f.detail, warn);

// overlay only: VPN tip names the label
fs = setupFindings({ lan: { lan: [], overlay: [{ ip: '203.0.113.7', adapter: 'Tailscale', label: 'internet via Tailscale' }], warning: null }, facts: null });
f = get(fs, 'overlay-only');
assert.equal(f.level, 'tip');
assert.match(f.detail, /internet via Tailscale/);
assert.match(f.fix, /Internet mode/);
assert.ok(!get(fs, 'no-address'));
assert.ok(!get(setupFindings({ lan: LAN, facts: null }), 'overlay-only'));

// client isolation: always a tip, last among tips
fs = setupFindings({ lan: LAN, facts: null });
assert.equal(fs.at(-1).id, 'client-isolation');
assert.match(fs.at(-1).fix, /client isolation/);

// order: problems, then tips, then ok
fs = setupFindings({ lan: { lan: [], overlay: [{ ip: '1.2.3.4', adapter: 'x', label: 'VPN' }] }, facts: fw('running', ['FedoraServer'], []) });
const rank = { problem: 0, tip: 1, ok: 2 };
assert.deepEqual(levels(fs), [...levels(fs)].sort((a, b) => rank[a] - rank[b]));
fs = setupFindings({ lan: LAN, facts: fw('running', ['FedoraWorkstation'], ['1025-65535/tcp', '1025-65535/udp']) });
assert.deepEqual(levels(fs), ['tip', 'ok']);
fs = setupFindings({ lan: { ...LAN, warning: warn }, facts: fw('running', ['FedoraServer'], []) });
assert.deepEqual(levels(fs), ['problem', 'problem', 'tip']);

// the plain words rule: no em or en dashes, no jargon, in any text
const all = [
  ...setupFindings({ lan: null, facts: fw('running', ['FedoraServer'], []) }),
  ...setupFindings({ lan: { ...LAN, warning: warn }, facts: fw('not_running', [], []) }),
  ...setupFindings({ lan: { lan: [], overlay: [{ label: 'VPN' }] }, facts: null }),
].flatMap((x) => [x.title, x.detail, x.fix]);
for (const t of all) assert.ok(!/[–—]|handshake/i.test(t), t);

// probe messages
let p = probeMessage({ kind: 'answered', ms: 14 }, '192.0.2.7');
assert.deepEqual(p, { level: 'ok', text: 'Reached 192.0.2.7 in 14 ms. A game is listening there.' });
p = probeMessage({ kind: 'refused' }, '192.0.2.7');
assert.equal(p.level, 'tip');
assert.match(p.text, /Start the game as host, then test again\./);
p = probeMessage({ kind: 'timed_out' }, '192.0.2.99');
assert.equal(p.level, 'problem');
assert.match(p.text, /^No answer from 192\.0\.2\.99\./);
assert.match(p.text, /port 47600/);
p = probeMessage({ kind: 'bad_address', message: 'Type an address like 192.0.2.10' }, 'x');
assert.deepEqual(p, { level: 'problem', text: 'Type an address like 192.0.2.10' });
for (const k of [{ kind: 'answered', ms: 1 }, { kind: 'refused' }, { kind: 'timed_out' }]) assert.ok(!/[–—]/.test(probeMessage(k, 'a').text));

console.log('setup check ok');

// Link Play setup check: turns what this PC reports (lan_report, setup_facts) and a connection test (link_probe) into plain findings
// for a player. Pure: no DOM, no Tauri, no fetch. It only builds text; a command in a fix is shown to the player, never run.
import { HOST_PORT } from './linkplay-model.js';

const ORDER = { problem: 0, tip: 1, ok: 2 };

/** True when one firewalld port entry ("47600/tcp", "1025-65535/udp") covers `port` for `proto`. */
function covers(entry, port, proto) {
  const m = /^(\d+)(?:-(\d+))?\/(tcp|udp)$/i.exec(String(entry ?? '').trim());
  if (!m || m[3].toLowerCase() !== proto) return false;
  const lo = Number(m[1]);
  const hi = m[2] === undefined ? lo : Number(m[2]);
  return lo <= port && port <= hi;
}

/** Findings as [{ id, level, title, detail, fix }], problems first, then tips, then ok. */
export function setupFindings({ lan, facts, port = HOST_PORT } = {}) {
  const out = [];
  const add = (id, level, title, detail, fix = '') => out.push({ id, level, title, detail, fix });
  const lans = lan?.lan ?? [];
  const overlays = lan?.overlay ?? [];

  if (!lans.length && !overlays.length) {
    add('no-address', 'problem', 'This PC has no network address to share',
      'No connected network was found, so nobody can reach this PC.',
      'Connect to Wi-Fi or plug in a network cable, then check again.');
  } else if (lan?.warning) {
    add('public-network', 'problem', 'Your network is set to Public network', lan.warning,
      lan.firewall_hint || 'Set the network to Private in your system settings.');
  }

  const fw = facts?.firewalld;
  if (fw?.state === 'running' && fw.zones?.length) {
    const ports = fw.ports ?? [];
    const open = covers_both(ports, port);
    const zones = fw.zones.join(', ');
    if (open) {
      add('firewall-open', 'ok', `The firewall allows port ${port}`, `The firewall zone ${zones} lets this port through.`);
    } else {
      add('firewall-blocks', 'problem', `The firewall may block port ${port}`,
        `The firewall zone ${zones} does not open port ${port} for both kinds of traffic, so other players may not get in.`,
        `Type this in a terminal, then check again: sudo firewall-cmd --add-port=${port}/tcp --add-port=${port}/udp`);
    }
  } else if (fw?.state === 'not_running') {
    add('firewall-off', 'tip', 'The firewall is switched off, so it is not the problem',
      'Nothing on this PC is filtering network traffic.');
  }

  if (!lans.length && overlays.length) {
    const names = [...new Set(overlays.map((o) => o.label || o.adapter).filter(Boolean))].join(', ') || 'a VPN';
    add('overlay-only', 'tip', 'Only a VPN address was found',
      `This PC only has an address from ${names}. Friends on your home network cannot use it.`,
      'Others must use Internet mode or an invite so they can reach you through the VPN.');
  }

  add('client-isolation', 'tip', 'If nobody can see each other on Wi-Fi, check the router',
    'Some routers stop devices on the same Wi-Fi from talking to each other.',
    "Ask the router owner to turn off 'client isolation' (guest networks often have it).");

  return out.map((f, i) => [f, i]).sort((a, b) => ORDER[a[0].level] - ORDER[b[0].level] || a[1] - b[1]).map(([f]) => f);
}

function covers_both(ports, port) {
  return ports.some((p) => covers(p, port, 'tcp')) && ports.some((p) => covers(p, port, 'udp'));
}

/** { level, text } for a link_probe result tried against `addr`. */
export function probeMessage(result, addr) {
  switch (result?.kind) {
    case 'answered':
      return { level: 'ok', text: `Reached ${addr} in ${result.ms} ms. A game is listening there.` };
    case 'refused':
      return { level: 'tip', text: 'The PC answered, but no game is hosting on that port. Start the game as host, then test again.' };
    case 'timed_out':
      return { level: 'problem', text: `No answer from ${addr}. A firewall may block port ${HOST_PORT}, the PC may be on another network, or it is switched off.` };
    case 'bad_address':
      return { level: 'problem', text: result.message || 'That does not look like an address. Type something like 192.168.1.20.' };
    default:
      return { level: 'problem', text: 'The test gave no usable answer. Try again.' };
  }
}

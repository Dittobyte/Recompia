// Tests for the pure co-op profile helpers (src/coop-model.js). Run: node launcher/scripts/test-coop-model.mjs
import { activeProfileOf, coopProfiles, isCoopProfile, normalizeRoomCode, relayProblem, roomCodeProblem, singleplayerProfiles } from '../src/coop-model.js';

let bad = 0;
const check = (ok, what) => { if (!ok) { console.log(`FAIL ${what}`); bad++; } };

check(normalizeRoomCode('abcd ol12 efgh 3456') === 'ABCD-0112-EFGH-3456', 'normalizes Crockford look-alikes and spaces');
check(normalizeRoomCode('ABCD-EFGH-JKMN-PQRS') === 'ABCD-EFGH-JKMN-PQRS', 'keeps normalized code');
check(normalizeRoomCode('short') === '', 'rejects short room code');
check(normalizeRoomCode('ABCD-EFGH-JKLM-NPQU') === '', 'rejects U');

check(roomCodeProblem('', { optional: true }) === '', 'optional empty room code allowed');
check(roomCodeProblem('') !== '', 'required empty room code rejected');
check(roomCodeProblem('abcd-efgh-jkmn-pqrs') === '', 'valid room code accepted');

check(relayProblem('') === '', 'empty relay means public relay');
check(relayProblem('203.0.113.5:47610') === '', 'valid IPv4:port relay accepted');
check(relayProblem('0.0.0.0:47610') !== '', '0.0.0.0 rejected');
check(relayProblem('example.com:47610') !== '', 'hostname rejected');

const sp = { id: 'default', name: 'Player' };
const cp = { id: 'p1', name: 'Island', coop: { room_code: 'ABCD-EFGH-JKMN-PQRS', world_version: 2, members: [] } };
const store = { active: 'p1', profiles: [sp, cp] };

check(!isCoopProfile(sp) && isCoopProfile(cp), 'identifies co-op vs singleplayer profiles');
check(activeProfileOf(store)?.id === 'p1', 'active profile lookup');
check(coopProfiles(store).length === 1 && coopProfiles(store)[0].id === 'p1', 'filters co-op profiles');
check(singleplayerProfiles(store).length === 1 && singleplayerProfiles(store)[0].id === 'default', 'filters singleplayer profiles');

console.log(bad ? `TEST FAILED (${bad})` : 'coop model tests passed');
process.exit(bad ? 1 : 0);

// Co-op profiles on the Link Play page: private room code, relay, group members, world version and co-op launch.
import { api } from '../api.js';
import { num, t } from '../i18n.js';
import { askText, errText, esc, group, icon, row, select, toast } from '../ui.js';
import { store } from '../store.js';
import { PUBLIC_RELAY } from '../linkplay-model.js';
import { activeProfileOf, coopProfiles, relayProblem, roomCodeProblem, singleplayerProfiles } from '../coop-model.js';

export function createCoopSection(host, { copy, gameActive }) {
  let profileStore = store.state.profiles;
  let pickedId = '';

  const currentCoop = () => {
    const list = coopProfiles(profileStore);
    if (!list.length) return null;
    const picked = list.find((p) => p.id === pickedId);
    if (picked) return picked;
    const active = activeProfileOf(profileStore);
    return active?.coop ? active : list[0];
  };

  async function refresh() {
    try {
      profileStore = await api.profilesList();
      store.refreshProfiles();
      draw();
    } catch (e) {
      console.warn('profiles_list', e);
    }
  }

  async function runUpdate(fn, done) {
    try {
      profileStore = await fn();
      await store.refreshProfiles();
      draw();
      if (done) toast(done, 'ok');
    } catch (e) {
      toast(errText(e), 'error');
    }
  }

  function draw() {
    if (!host) return;
    const busy = gameActive(store.state) || !!store.state.starting || !!store.state.task;
    const list = coopProfiles(profileStore);
    const spList = singleplayerProfiles(profileStore);
    const cp = currentCoop();

    if (!cp) {
      const sp = spList[0];
      const actions = [
        `<button class="btn primary sm" data-coop="new" ${busy ? 'disabled' : ''}>${icon('plus')}${esc(t('New co-op profile'))}</button>`,
        ...(sp ? [`<button class="btn sm" data-coop="from-sp" data-id="${esc(sp.id)}" data-name="${esc(sp.name)}" ${busy ? 'disabled' : ''}>${icon('users')}${esc(t('Start coop from my singleplayer save'))}</button>`] : []),
      ].join('');
      host.innerHTML = group(t('Co-op'), [
        row(t('Co-op saves'), `<div class="cluster">${actions}</div>`),
      ]) + `<div class="banner info">${icon('info')}<div class="grow">${esc(t('Co-op saves use their own folder, apply co-op settings in the environment for that launch only (without touching pkp.ini), and connect to a private room on the relay.'))}</div></div>`;
      return;
    }

    pickedId = cp.id;
    const coop = cp.coop;
    const relayText = coop.relay ? coop.relay : t('Public relay ({addr})', { addr: PUBLIC_RELAY });
    const members = coop.members ?? [];
    const memberRows = members.map((m, i) => row(
      m.name,
      `<span class="set-value">${esc(m.role === 'host' ? t('Host') : m.role === 'visitor' ? t('Visitor') : '')}</span>
       <button class="btn quiet sm" data-coop="rm-member" data-idx="${i}" ${busy ? 'disabled' : ''} title="${esc(t('Remove'))}">${icon('trash-2')}</button>`,
    ));

    const headerCtrl = `<div class="cluster">
      ${list.length > 1 ? select('lp-coop-pick', list.map((p) => [p.id, p.name]), cp.id, { label: t('Co-op profile') }) : `<strong class="set-value">${esc(cp.name)}</strong>`}
      <button class="btn primary sm" data-coop="play" ${busy ? 'disabled' : ''}>${icon('play')}${esc(t('Play co-op'))}</button>
      <button class="btn sm" data-coop="new" ${busy ? 'disabled' : ''}>${icon('plus')}${esc(t('New'))}</button>
    </div>`;

    const codeCtrl = `<div class="cluster">
      <code class="selectable">${esc(coop.room_code)}</code>
      <button class="btn quiet sm lp-copy" data-coop="copy-code" aria-label="${esc(t('Copy {text}', { text: coop.room_code }))}" title="${esc(t('Copy'))}">${icon('copy')}</button>
      <button class="btn sm" data-coop="edit-code" ${busy ? 'disabled' : ''}>${icon('pencil')}${esc(t('Change'))}</button>
      <button class="btn sm" data-coop="new-code" ${busy ? 'disabled' : ''}>${icon('refresh-cw')}${esc(t('New code'))}</button>
    </div>`;

    const relayCtrl = `<div class="cluster">
      <code class="selectable">${esc(relayText)}</code>
      <button class="btn sm" data-coop="edit-relay" ${busy ? 'disabled' : ''}>${icon('pencil')}${esc(t('Change'))}</button>
    </div>`;

    const spBtn = spList[0]
      ? `<button class="btn sm" data-coop="from-sp" data-id="${esc(spList[0].id)}" data-name="${esc(spList[0].name)}" ${busy ? 'disabled' : ''}>${icon('users')}${esc(t('Start coop from my singleplayer save'))}</button>`
      : '';

    const html = group(t('Co-op'), [
      row(t('Co-op profile'), headerCtrl),
      row(t('Private room code'), codeCtrl),
      row(t('Relay'), relayCtrl),
      row(t('World version'), `<span class="num">v${esc(num(coop.world_version ?? 0))}</span>`),
      ...memberRows,
      row(t('Group members'), `<div class="cluster">
        <button class="btn sm" data-coop="add-member" ${busy ? 'disabled' : ''}>${icon('plus')}${esc(t('Add member'))}</button>
        <button class="btn sm" data-coop="to-sp" ${busy ? 'disabled' : ''}>${icon('copy')}${esc(t('Copy to singleplayer'))}</button>
        ${spBtn}
      </div>`),
    ]) + `<div class="banner info">${icon('info')}<div class="grow">${esc(t('Private co-op rooms never appear on the public room list. Co-op settings are passed for that launch only and never saved to pkp.ini.'))}</div></div>`;

    if (host.dataset.html !== html) {
      host.innerHTML = html;
      host.dataset.html = html;
    }
  }

  host.addEventListener('change', (e) => {
    if (e.target.id !== 'lp-coop-pick') return;
    pickedId = e.target.value;
    draw();
  });

  host.addEventListener('click', async (e) => {
    const btn = e.target.closest('[data-coop]');
    if (!btn || btn.disabled) return;
    const cp = currentCoop();
    switch (btn.dataset.coop) {
      case 'play':
        if (cp) store.playCoop({ id: cp.id });
        return;
      case 'copy-code':
        if (cp) copy(cp.coop.room_code);
        return;
      case 'new': {
        const name = await askText({ title: t('New co-op profile'), body: t('Starts a co-op save in its own folder with a private room on the relay.'), label: t('Profile name'), ok: t('Next') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        const code = await askText({ title: t('Private room code'), body: t('Leave blank to make a new private room, or enter your group\'s room code.'), label: t('Room code'), ok: t('Add'), max: 24 });
        if (code === null) return;
        if (code && roomCodeProblem(code, { optional: true })) return toast(roomCodeProblem(code, { optional: true }), 'error');
        await runUpdate(async () => {
          const s = await api.profileAddCoop({ name, roomCode: code || null, relay: null });
          pickedId = s.profiles[s.profiles.length - 1]?.id ?? '';
          return s;
        }, t('Co-op profile {name} added', { name }));
        return;
      }
      case 'from-sp': {
        const sourceId = btn.dataset.id;
        const sourceName = btn.dataset.name;
        const name = await askText({ title: t('Start coop from my singleplayer save'), body: t('Copies {name}\'s save into a new co-op profile. Your singleplayer save stays untouched.', { name: sourceName }), label: t('Co-op profile name'), value: `${sourceName} Co-op`.slice(0, 24), ok: t('Next') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        const code = await askText({ title: t('Private room code'), body: t('Leave blank to make a new private room, or enter your group\'s room code.'), label: t('Room code'), ok: t('Start co-op'), max: 24 });
        if (code === null) return;
        if (code && roomCodeProblem(code, { optional: true })) return toast(roomCodeProblem(code, { optional: true }), 'error');
        await runUpdate(async () => {
          const s = await api.profileStartCoopFrom({ sourceId, name, roomCode: code || null, relay: null });
          pickedId = s.profiles[s.profiles.length - 1]?.id ?? '';
          return s;
        }, t('Co-op profile {name} added', { name }));
        return;
      }
      case 'to-sp': {
        if (!cp) return;
        const name = await askText({ title: t('Copy to singleplayer'), body: t('Copies {name}\'s co-op save into a new singleplayer profile.', { name: cp.name }), label: t('Profile name'), ok: t('Copy') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        await runUpdate(() => api.profileCopyToSingleplayer({ sourceId: cp.id, name }), t('Profile {name} added', { name }));
        return;
      }
      case 'edit-code': {
        if (!cp) return;
        const code = await askText({ title: t('Private room code'), body: t('Enter the 16-character room code shared by your group.'), label: t('Room code'), value: cp.coop.room_code, ok: t('Save'), max: 24 });
        if (code === null) return;
        const prob = roomCodeProblem(code);
        if (prob) return toast(prob, 'error');
        await runUpdate(() => api.profileCoopUpdate({ id: cp.id, roomCode: code, relay: null, members: null }), t('Room code updated'));
        return;
      }
      case 'new-code':
        if (!cp) return;
        await runUpdate(() => api.profileCoopUpdate({ id: cp.id, roomCode: '', relay: null, members: null }), t('New room code created'));
        return;
      case 'edit-relay': {
        if (!cp) return;
        const relay = await askText({ title: t('Co-op relay'), body: t('Leave empty to use the public relay ({addr}), or enter a custom relay IPv4:port.', { addr: PUBLIC_RELAY }), label: t('Relay address'), value: cp.coop.relay, ok: t('Save'), max: 32 });
        if (relay === null) return;
        const prob = relayProblem(relay);
        if (prob) return toast(prob, 'error');
        await runUpdate(() => api.profileCoopUpdate({ id: cp.id, roomCode: null, relay, members: null }), t('Relay updated'));
        return;
      }
      case 'add-member': {
        if (!cp) return;
        const name = await askText({ title: t('Add member'), label: t('Member name'), ok: t('Add') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        const members = [...(cp.coop.members ?? []), { name, net_uid: '', role: 'visitor' }];
        await runUpdate(() => api.profileCoopUpdate({ id: cp.id, roomCode: null, relay: null, members }), t('Member added'));
        return;
      }
      case 'rm-member': {
        if (!cp) return;
        const idx = Number(btn.dataset.idx);
        const members = (cp.coop.members ?? []).filter((_, i) => i !== idx);
        await runUpdate(() => api.profileCoopUpdate({ id: cp.id, roomCode: null, relay: null, members }), t('Member removed'));
        return;
      }
    }
  });

  refresh();
  return { draw, refresh };
}

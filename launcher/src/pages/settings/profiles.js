// Save profiles: add, switch, rename, remove. Each goes through changeProfiles: flush pending edits, run the command, reload the
// settings (a profile has its own save folder), say what happened.
import { api } from '../../api.js';
import { t } from '../../i18n.js';
import { store } from '../../store.js';
import { askText, confirm, errText, toast } from '../../ui.js';

export function createProfileActions(h) {
  const { st } = h;

  function loadLists() {
    api.profilesList().then((list) => {
      st.profiles = list;
      h.draw();
    }).catch((e) => console.warn('profiles_list', e));
    api.profilesPlayTime().then((times) => {
      st.playTimes = times;
      h.draw();
    }).catch((e) => console.warn('profiles_play_time', e));
  }

  async function changeProfiles(command, args, done) {
    try {
      await h.flush();
      st.profiles = await command(args);
      await store.refreshProfiles();
      await h.reload();
      toast(done, 'ok');
    } catch (e) {
      toast(errText(e), 'error');
    }
    h.draw();
  }

  return {
    load: loadLists,
    reload: loadLists,
    actions: {
      async 'profile-new'() {
        const name = await askText({ title: t('New profile'), body: t('It starts with an empty save. Your other saves are not touched.'), label: t('Profile name'), ok: t('Add') });
        if (name === '') toast(t('Type a name for the profile'), 'error');
        else if (name) await changeProfiles(api.profileAdd, { name }, t('Profile {name} added', { name }));
      },
      async 'profile-new-coop'() {
        const name = await askText({ title: t('New co-op profile'), body: t('Starts a co-op save in its own folder with a private room on the relay.'), label: t('Profile name'), ok: t('Next') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        const code = await askText({ title: t('Private room code'), body: t('Leave blank to make a new private room, or enter your group\'s room code.'), label: t('Room code'), ok: t('Add'), max: 24 });
        if (code === null) return;
        await changeProfiles(api.profileAddCoop, { name, roomCode: code || null, relay: null }, t('Co-op profile {name} added', { name }));
      },
      async 'profile-start-coop'(arg, sourceName) {
        const name = await askText({ title: t('Start coop from my singleplayer save'), body: t('Copies {name}\'s save into a new co-op profile. Your singleplayer save stays untouched.', { name: sourceName }), label: t('Co-op profile name'), value: `${sourceName} Co-op`.slice(0, 24), ok: t('Next') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        if (!name) return;
        const code = await askText({ title: t('Private room code'), body: t('Leave blank to make a new private room, or enter your group\'s room code.'), label: t('Room code'), ok: t('Start co-op'), max: 24 });
        if (code === null) return;
        await changeProfiles(api.profileStartCoopFrom, { sourceId: arg, name, roomCode: code || null, relay: null }, t('Co-op profile {name} added', { name }));
      },
      async 'profile-copy-singleplayer'(arg, sourceName) {
        const name = await askText({ title: t('Copy to singleplayer'), body: t('Copies {name}\'s co-op save into a new singleplayer profile.', { name: sourceName }), label: t('Profile name'), ok: t('Copy') });
        if (name === '') return toast(t('Type a name for the profile'), 'error');
        else if (name) await changeProfiles(api.profileCopyToSingleplayer, { sourceId: arg, name }, t('Profile {name} added', { name }));
      },
      'profile-use': (arg, name) => changeProfiles(api.profileSelect, { id: arg }, t('Playing as {name}', { name })),
      async 'profile-rename'(arg, name) {
        const next = await askText({ title: t('Rename profile'), label: t('Profile name'), value: name, ok: t('Rename') });
        if (next === '') toast(t('Type a name for the profile'), 'error');
        else if (next && next !== name) await changeProfiles(api.profileRename, { id: arg, name: next }, t('Profile renamed'));
      },
      async 'profile-remove'(arg, name) {
        const yes = await confirm({
          title: t('Remove {name}?', { name }),
          body: t('This only removes the profile from the list. Its save stays in the save folder, so nothing is lost.'),
          ok: t('Remove'), danger: true,
        });
        if (yes) await changeProfiles(api.profileRemove, { id: arg }, t('Profile removed'));
      },
    },
  };
}

// The Layout panel: a modal <dialog> where the player picks the sidebar position, spacing, item order and the Home sections.
// Every change is applied at once through setLayout() (live preview); the panel redraws from currentLayout().
import './layout-panel.css';
import { currentLayout, setLayout } from './layout-apply.js';
import { DEFAULT_LAYOUT, LOCKED, setNav, setDensity, moveItem, setHidden, setBlock } from './layout-model.js';
import { esc, icon, segmented, toggle } from './ui.js';

const NAV_OPTIONS = [['left', 'Left'], ['right', 'Right'], ['top', 'Top'], ['bottom', 'Bottom'], ['icons', 'Icons only']];
const DENSITY_OPTIONS = [['comfortable', 'Comfortable'], ['compact', 'Compact']];
const FALLBACK_LABELS = { home: 'Home', linkplay: 'Link Play', news: "What's New", settings: 'Settings' };
const BLOCK_LABELS = [['news', 'News cards'], ['link', 'Link Play shortcut']];

/** The sidebar's own label for a page, so the panel never disagrees with it. */
function labelOf(id) {
  const btn = document.querySelector(`#nav [data-page="${id}"]`) || document.querySelector(`[data-page="${id}"]`);
  return btn?.getAttribute('aria-label') || FALLBACK_LABELS[id] || id;
}

function itemRow(l, id, index) {
  const locked = LOCKED.includes(id);
  const last = l.order.length - 1;
  return `<li class="lp-item" data-id="${esc(id)}">
    <span class="lp-name">${esc(labelOf(id))}</span>
    <span class="lp-tools">
      <button class="btn icon-only lp-move" type="button" data-act="up" data-id="${esc(id)}" aria-label="Move ${esc(labelOf(id))} up" ${index === 0 ? 'disabled' : ''}>${icon('chevron-down', 'lp-flip')}</button>
      <button class="btn icon-only lp-move" type="button" data-act="down" data-id="${esc(id)}" aria-label="Move ${esc(labelOf(id))} down" ${index === last ? 'disabled' : ''}>${icon('chevron-down')}</button>
      ${locked
        ? '<span class="lp-note">always shown</span>'
        : `<span class="lp-show"><span class="lp-showlabel" aria-hidden="true">Show</span>${toggle(`lp-show-${id}`, !l.hidden.includes(id), { label: `Show ${labelOf(id)} in the sidebar` })}</span>`}
    </span>
  </li>`;
}

function body(l) {
  return `
    <h2 id="lp-title">Layout</h2>
    <section class="lp-sec"><h3>Sidebar</h3>${segmented('lp-nav', NAV_OPTIONS, l.nav, { label: 'Sidebar position' })}</section>
    <section class="lp-sec"><h3>Spacing</h3>${segmented('lp-density', DENSITY_OPTIONS, l.density, { label: 'Spacing' })}</section>
    <section class="lp-sec"><h3>Sidebar items</h3><ul class="lp-list">${l.order.map((id, i) => itemRow(l, id, i)).join('')}</ul></section>
    <section class="lp-sec"><h3>Home sections</h3>
      ${BLOCK_LABELS.map(([id, text]) => `<div class="lp-block"><label class="lp-name" for="lp-block-${id}">${esc(text)}</label>${toggle(`lp-block-${id}`, l.blocks[id], { label: text })}</div>`).join('')}
    </section>`;
}

/** What to focus again after a redraw: the same control, found by a stable key. */
const keyOf = (el) => {
  if (!el) return '';
  if (el.name) return `input[name="${el.name}"][value="${el.value}"]`;
  if (el.id) return `#${el.id}`;
  if (el.dataset?.act) return `[data-act="${el.dataset.act}"][data-id="${el.dataset.id}"]`;
  return '';
};

export function openLayoutPanel() {
  if (document.querySelector('dialog.layout-panel')) return;
  const opener = document.getElementById('layout-btn');
  const dlg = document.createElement('dialog');
  dlg.className = 'modal glass strong layout-panel';
  dlg.setAttribute('aria-labelledby', 'lp-title');
  dlg.innerHTML = `
    <div class="modal-body lp-body"></div>
    <div class="modal-actions lp-actions">
      <button class="btn" type="button" data-act="reset">Reset to default</button>
      <button class="btn primary" type="button" data-answer="">Close</button>
    </div>`;
  const host = dlg.querySelector('.lp-body');

  const draw = () => {
    const key = keyOf(document.activeElement && dlg.contains(document.activeElement) ? document.activeElement : null);
    const scroll = dlg.scrollTop;
    host.innerHTML = body(currentLayout());
    dlg.scrollTop = scroll;
    if (key) {
      let el = dlg.querySelector(key);
      if (el?.disabled) el = dlg.querySelector('.lp-body input:checked, .lp-body button:not(:disabled)');   // a Move button that just hit the end
      el?.focus();
    }
  };

  dlg.addEventListener('change', (e) => {
    const t = e.target;
    const l = currentLayout();
    if (t.name === 'lp-nav') setLayout(setNav(l, t.value));
    else if (t.name === 'lp-density') setLayout(setDensity(l, t.value));
    else if (t.id?.startsWith('lp-show-')) setLayout(setHidden(l, t.id.slice(8), !t.checked));
    else if (t.id?.startsWith('lp-block-')) setLayout(setBlock(l, t.id.slice(9), t.checked));
    else return;
    draw();
  });
  dlg.addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (b?.hasAttribute('data-answer')) { dlg.close(''); return; }
    if (e.target === dlg) { dlg.close(''); return; }   // the dialog has no padding, so only the backdrop hits it
    const act = b?.dataset.act;
    if (act === 'up' || act === 'down') setLayout(moveItem(currentLayout(), b.dataset.id, act === 'up' ? -1 : 1));
    else if (act === 'reset') setLayout(structuredClone(DEFAULT_LAYOUT));
    else return;
    draw();
  });
  dlg.addEventListener('cancel', (e) => { e.preventDefault(); dlg.close(''); });   // Escape: one close path
  dlg.addEventListener('close', () => {
    dlg.remove();
    (document.getElementById('layout-btn') || opener)?.focus();
  });

  document.body.appendChild(dlg);
  draw();
  dlg.showModal();
  dlg.querySelector('input[name="lp-nav"]:checked, input[name="lp-nav"]')?.focus();
}

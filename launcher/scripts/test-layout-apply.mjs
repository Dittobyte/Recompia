// Checks layout-apply.js with a fake localStorage and document. Needs src/layout-model.js (merged from LM-L1-01).
import assert from 'node:assert/strict';
const attrs = {};
const mkBtn = (id) => ({ dataset: { page: id }, style: {}, hidden: false });
const btns = ['home', 'linkplay', 'news', 'settings'].map(mkBtn);
const store = {};
let throwing = false;
globalThis.localStorage = {
  getItem: (k) => { if (throwing) throw new Error('x'); return k in store ? store[k] : null; },
  setItem: (k, v) => { if (throwing) throw new Error('x'); store[k] = v; },
};
globalThis.document = {
  documentElement: { dataset: attrs, removeAttribute: (k) => { delete attrs[k]; }, setAttribute: (k, v) => { attrs[k] = v; } },
  querySelectorAll: () => btns,
};
const events = [];
globalThis.window = { dispatchEvent: (e) => events.push(e) };
globalThis.CustomEvent = class { constructor(t, o) { this.type = t; this.detail = o?.detail; } };

const A = await import('../src/layout-apply.js');
const M = await import('../src/layout-model.js');

assert.deepEqual(A.loadLayout(), M.normalize(M.DEFAULT_LAYOUT), 'empty storage gives default');
store.layout = '{not json';
assert.deepEqual(A.loadLayout(), M.normalize(M.DEFAULT_LAYOUT), 'bad json gives default');

const l = M.normalize({ ...M.DEFAULT_LAYOUT, nav: 'top', density: 'compact', order: ['settings', 'home', 'news', 'linkplay'], hidden: ['news'], blocks: { news: false, link: true } });
const out = A.setLayout(l);
assert.equal(store.layout, M.serialize(l), 'saved');
assert.deepEqual(A.loadLayout(), out, 'load after save');
assert.equal(attrs.nav, 'top'); assert.equal(attrs.density, 'compact');
assert.ok('data-hide-news' in attrs); assert.ok(!('data-hide-link' in attrs));
assert.equal(btns.find((b) => b.dataset.page === 'settings').style.order, '0');
assert.equal(btns.find((b) => b.dataset.page === 'news').hidden, true);
assert.equal(btns.find((b) => b.dataset.page === 'home').hidden, false);
assert.equal(events.at(-1).type, A.LAYOUT_EVENT);
assert.deepEqual(A.currentLayout(), out);
A.applyLayout(M.DEFAULT_LAYOUT);
assert.ok(!('data-hide-news' in attrs), 'attribute removed when block on');

throwing = true;
assert.deepEqual(A.loadLayout(), M.normalize(M.DEFAULT_LAYOUT), 'throwing storage on load');
A.saveLayout(l); A.setLayout(l);
console.log('layout-apply: ok');

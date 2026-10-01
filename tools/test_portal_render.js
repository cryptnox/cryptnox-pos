/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 *
 * test_portal_render.js — headless test of the config portal page's render logic.
 *
 * `check_portal_page.py` proves the script parses and that its element ids exist.
 * This proves it shows the right thing: which sections are visible for each
 * (mode, step, authed, pending) the device can report. That logic is a hand-written
 * pile of booleans in `render()`, it decides whether the operator can complete
 * setup at all, and nothing else tests it.
 *
 * What it is specifically there to catch:
 *   - an unauthorised browser being shown anything it could submit;
 *   - the wizard showing two steps at once, or none;
 *   - a value waiting on the panel while the page still invites another;
 *   - the admin page growing a wizard Continue button, or vice versa.
 *
 * Run from the repo root (needs python for the extractor):
 *
 *   python tools/check_portal_page.py --emit-js build/portal_page.js
 *   node tools/test_portal_render.js build/portal_page.js
 */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const src = process.argv[2];
if (!src) {
  console.error('usage: node tools/test_portal_render.js <extracted page.js>');
  process.exit(2);
}

const ids = fs.readFileSync(src.replace(/\.js$/, '.ids'), 'utf8')
  .split('\n').map(s => s.trim()).filter(Boolean);

/* ── A DOM only as real as this page needs ──────────────────────────────────
 * getElementById throws on an unknown id rather than returning null: the page
 * would throw anyway on the next property access, and failing at the lookup names
 * the id in the stack trace. */
function makeEl(id) {
  return {
    id,
    hidden: false,
    textContent: '',
    value: '',
    type: '',
    checked: false,
    disabled: false,
    onclick: null,
    files: [],
    children: [],
    attrs: {},
    clicked: 0,
    appendChild(c) { this.children.push(c); return c; },
    setAttribute(k, v) { this.attrs[k] = String(v); },
    click() { this.clicked++; },
    focus() { this.focused = true; },
  };
}

const els = new Map(ids.map(id => [id, makeEl(id)]));
const document = {
  getElementById(id) {
    if (!els.has(id)) { throw new Error(`page asked for unknown id '${id}'`); }
    return els.get(id);
  },
  createElement(tag) { return makeEl(`<${tag}>`); },
};

/* ── A fake terminal ────────────────────────────────────────────────────────
 * Starts inert — every fetch hangs — so the synchronous render scenarios below
 * cannot be raced by a resolving promise. The authorisation handshake test at the
 * end switches it live.
 *
 * It enforces the token exactly as the device does, which is the point: the real
 * `state_get()` answers an unauthorised request with a deliberately minimal body,
 * so a page that forgets the header on ONE of its requests can never observe that
 * it was authorised. That was a real bug and this is what catches it.
 */
const srv = {
  live: false,
  granted: false,   /* the operator has typed the code on the panel */
  pending: false,
  token: null,
  asks: 0,          /* times the panel was actually told to ask */
  seen: [],         /* {url, token} per request */
};

function reply(body) {
  const text = JSON.stringify(body);
  return Promise.resolve({
    ok: true, status: 200,
    json: () => Promise.resolve(body),
    text: () => Promise.resolve(text),
  });
}

function fakeFetch(url, opts) {
  if (!srv.live) { return new Promise(() => {}); }
  const hdr = (opts && opts.headers) || {};
  const token = hdr['X-Prov-Token'];
  srv.seen.push({ url, token });

  if (url === '/api/state') {
    /* Mirrors state_get(): two different bodies, and which one you get depends
     * entirely on whether you presented the token. */
    if (srv.granted && token === srv.token) {
      /* srv.state lets a scenario put the device in a specific step — the
       * wizard's last one needs it. Unset means the admin default below. */
      return reply(srv.state || {
        mode: 'admin', step: 'admin', authed: true, auth_pending: false,
        version: '1.0.0', pay_eth: '0xAAA', pay_trx: 'TBBB',
        ct_eth: '0xCCC', ct_trx: 'TDDD', ssid: 'Lucky_2.4G',
        pending: '', note: '', scan_gen: 0, win: 15,
      });
    }
    return reply({
      mode: 'admin', step: 'admin', authed: false,
      auth_pending: srv.pending, version: '1.0.0',
    });
  }
  if (url === '/api/auth') {
    /* Mirrors auth_post(): already granted hands the token back WITHOUT asking
     * the panel again, which is why "Ask again" is silent once authorised. */
    if (srv.granted) { return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve(srv.token) }); }
    srv.token = 'a'.repeat(32);
    srv.pending = true;
    srv.asks++;
    /* Where the token starts existing. Requests before this point cannot carry one
     * and must not be held to it; everything after must. */
    srv.tokenAt = srv.seen.length;
    return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve(srv.token) });
  }
  if (url === '/api/scan') { return reply({ aps: [] }); }
  return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve('') });
}

const sandbox = {
  document,
  fetch: fakeFetch,
  XMLHttpRequest: function () { this.open = this.setRequestHeader = this.send = () => {}; this.upload = {}; },
  setInterval: () => 0,
  /* The upload stops the state poll for its duration — see the socket budget in
   * prov_start(). Stubbed so evaluating the page does not throw. */
  clearInterval: () => {},
  Promise, Math, JSON, String, Number, encodeURIComponent, console,
};

/* Exported by appending to the extracted source: `render` and `S` are module-scope
 * in the page, so the epilogue has to be evaluated in the same scope. */
const code = fs.readFileSync(src, 'utf8') +
  '\n;__out.render=render;__out.poll=poll;' +
  '__out.setS=function(v){S=v};__out.getS=function(){return S};';
sandbox.__out = {};
vm.createContext(sandbox);
vm.runInContext(code, sandbox, { filename: path.basename(src) });
const page = sandbox.__out;

/* ── The contract ──────────────────────────────────────────────────────────
 * `on` lists the sections that must be visible; every other section must be
 * hidden. Listing only the positives and deriving the negatives is deliberate: a
 * new section added to the page with no case here fails every scenario at once,
 * rather than silently defaulting to visible.
 */
const SECTIONS = ['s_auth', 's_pend', 's_addr', 's_net', 's_ct', 's_fee',
  's_wifi', 's_fw', 's_final', 'nav'];

const CASES = [
  {
    name: 'wizard, not yet authorised',
    state: { mode: 'wizard', step: 'auth', authed: false, version: '1.0.0' },
    on: ['s_auth'],
    also: s => assert.strictEqual(els.get('waiting').hidden, true,
      'the waiting box should appear only once the terminal has been asked'),
  },
  {
    name: 'wizard, asked, waiting for the code on the panel',
    state: { mode: 'wizard', step: 'auth', authed: false, auth_pending: true },
    on: ['s_auth'],
    also: () => assert.strictEqual(els.get('waiting').hidden, false),
  },
  {
    name: 'wizard, authorised, address step',
    state: { mode: 'wizard', step: 'addr', authed: true, pay_eth: '', pay_trx: '' },
    on: ['s_addr', 'nav'],
  },
  {
    /* No Continue here: joining is what ends the wizard, so a button that only
     * repaints the panel would be the one thing on screen doing nothing. */
    name: 'wizard, authorised, Wi-Fi step',
    state: { mode: 'wizard', step: 'wifi', authed: true },
    on: ['s_wifi'],
  },
  {
    /* The step the panel shows when setup is over. The page has no screen for it
     * and cannot: reaching PROV_STEP_DONE means the portal has been stopped, so
     * nothing answers the poll that would have reported it. The wizard's own last
     * screen is s_final, driven by the page (see the end-of-wizard case below).
     * Asserted anyway, because "no section at all" has to be deliberate rather
     * than a hole. */
    name: 'wizard, finished (a step the browser can never see)',
    state: { mode: 'wizard', step: 'done', authed: true },
    on: [],
  },
  {
    name: 'wizard, a value waiting on the panel',
    state: { mode: 'wizard', step: 'addr', authed: true, pending: 'Ethereum payout address' },
    on: ['s_pend'],
    also: () => assert.strictEqual(els.get('pend').textContent, 'Ethereum payout address'),
  },
  {
    name: 'admin, not authorised',
    state: { mode: 'admin', step: 'admin', authed: false },
    on: ['s_auth'],
  },
  {
    name: 'admin, authorised',
    state: {
      mode: 'admin', step: 'admin', authed: true,
      pay_eth: '0xAAA', pay_trx: 'TBBB', ct_eth: '0xCCC', ct_trx: 'TDDD',
      ssid: 'My Cafe', version: '1.0.1', scan_gen: 0,
      fee_max: 40, fee_prio: 3, mainnet: true, ct_eth_own: true,
    },
    on: ['s_addr', 's_net', 's_ct', 's_fee', 's_wifi', 's_fw'],
    also: () => {
      assert.strictEqual(els.get('cur_eth').textContent, '0xAAA');
      assert.strictEqual(els.get('cur_trx').textContent, 'TBBB');
      assert.strictEqual(els.get('cur_cte').textContent, '0xCCC');
      assert.strictEqual(els.get('cur_ctt').textContent, 'TDDD');
      /* A contract nobody overrode is not a missing one — it is the firmware's
       * own, doing its job. Reporting that as "not set" beside a working USDC
       * selection is how an operator comes to paste over an address that was
       * already right, so the line under each says where it came from. */
      assert.match(els.get('src_cte').textContent, /Set by an operator/);
      assert.match(els.get('src_ctt').textContent, /Built into this firmware/);
      assert.strictEqual(els.get('cur_ssid').textContent, 'My Cafe');
      /* Shown with a 'v' though the JSON carries it bare, so that nothing
       * parsing /api/state has to strip it back off. */
      assert.strictEqual(els.get('ver').textContent, 'v1.0.1');
      /* The gas caps are the settings the panel no longer edits, so this page is
       * the only place they can be changed — and a field that never shows the
       * stored number is one an operator overwrites blind. */
      assert.strictEqual(els.get('cur_fmax').textContent, 40);
      assert.strictEqual(els.get('cur_fprio').textContent, 3);
      assert.strictEqual(els.get('in_fmax').value, 40, 'the max fee should be seeded');
      assert.strictEqual(els.get('in_fprio').value, 3, 'the tip should be seeded');
      /* Which networks a sale settles on is the one setting on this page whose
       * wrong value costs a whole shift's takings, so the page has to report it
       * from the device rather than from whatever the radio happened to be on. */
      assert.strictEqual(els.get('cur_net').textContent, 'production');
      assert.strictEqual(els.get('net_main').checked, true,
        'the radio should be seeded from the device');
      assert.strictEqual(els.get('net_test').checked, false);
    },
  },
  {
    /* The same page against a test-network terminal. Its own case rather than a
     * second assertion on the one above: the seeding runs once and only once, and
     * a bug that always ticks "production" passes every check that never renders
     * a terminal on a testnet. */
    name: 'admin, on the test networks',
    state: {
      mode: 'admin', step: 'admin', authed: true,
      pay_eth: '0xAAA', pay_trx: 'TBBB', ssid: 'My Cafe', scan_gen: 0,
      fee_max: 40, fee_prio: 3, mainnet: false,
    },
    on: ['s_addr', 's_net', 's_ct', 's_fee', 's_wifi', 's_fw'],
    also: () => {
      assert.strictEqual(els.get('cur_net').textContent, 'test networks');
      assert.strictEqual(els.get('cur_cte').textContent, 'none configured');
      assert.match(els.get('src_cte').textContent, /refused/);
      assert.strictEqual(els.get('net_test').checked, true);
      assert.strictEqual(els.get('net_main').checked, false);
    },
  },
  {
    name: 'admin, a value waiting on the panel',
    state: { mode: 'admin', step: 'admin', authed: true, pending: 'TRC-20 token contract' },
    on: ['s_pend'],
  },
];

let failures = 0;
for (const c of CASES) {
  /* Reset every section, so a case can only pass by being shown by render(). */
  for (const id of SECTIONS.concat(['waiting'])) { els.get(id).hidden = null; }
  /* And the network radios, because render() seeds them once and then leaves
   * them alone — a page the operator has touched must not have its choice taken
   * back by the next poll. Each case is a fresh page load, so clear them. */
  for (const id of ['net_main', 'net_test']) { els.get(id).checked = false; }
  page.setS(c.state);
  try {
    page.render();
    for (const id of SECTIONS) {
      const want = c.on.includes(id);
      assert.strictEqual(els.get(id).hidden, !want,
        `${id} should be ${want ? 'visible' : 'hidden'}`);
    }
    if (c.also) { c.also(c.state); }
    console.log(`  ok    ${c.name}`);
  } catch (e) {
    console.log(`  FAIL  ${c.name}\n        ${e.message}`);
    failures++;
  }
}

/* render() must not *write* address data for an unauthorised session. The device
 * already declines to send any (state_get has two different response bodies), so
 * this is the belt to that braces: if the two ever disagree, the page must not be
 * the thing that puts a payout address on screen.
 *
 * Asserted as "did not write" rather than "is empty", because a session that was
 * authorised and then dropped leaves its last values in the DOM — inside a section
 * render() has just hidden, which is not a leak and not worth clearing. */
try {
  const SENTINEL = '<<untouched>>';
  for (const id of ['cur_eth', 'cur_trx', 'cur_cte', 'cur_ctt', 'cur_ssid']) {
    els.get(id).textContent = SENTINEL;
  }
  page.setS({
    mode: 'wizard', step: 'auth', authed: false,
    /* A device that wrongly volunteered them anyway. */
    pay_eth: '0xLEAK', pay_trx: 'TLEAK', ssid: 'Leaky Cafe',
  });
  page.render();
  for (const id of ['cur_eth', 'cur_trx', 'cur_cte', 'cur_ctt', 'cur_ssid']) {
    assert.strictEqual(els.get(id).textContent, SENTINEL,
      `${id} was written for an unauthorised session`);
  }
  console.log('  ok    unauthorised session renders no addresses');
} catch (e) {
  console.log(`  FAIL  unauthorised session renders no addresses\n        ${e.message}`);
  failures++;
}

/* ── The typed send-to fields are always there ──────────────────────────────
 * They used to be hidden behind a "Manual input" button, which left the card
 * read looking like the only way to set a payout address — and that sets it to
 * the tapped card. So they are open on arrival, and render() (every 1.5 s) must
 * never close them on whoever is typing into one.
 */
try {
  page.setS({ mode: 'admin', step: 'admin', authed: true, scan_gen: 0 });
  page.render();
  els.get('in_eth').value = '0xhalf-typed';
  els.get('in_trx').value = 'Thalf-typed';
  page.render();
  assert.strictEqual(els.get('s_addr').hidden, false,
    'render() hid the send-to card on an authorised admin page');
  assert.strictEqual(els.get('in_eth').value, '0xhalf-typed',
    'a poll cleared a half-typed Ethereum address');
  assert.strictEqual(els.get('in_trx').value, 'Thalf-typed',
    'a poll cleared a half-typed Tron address');
  console.log('  ok    the typed send-to fields stay open');
} catch (e) {
  console.log(`  FAIL  the typed send-to fields stay open\n        ${e.message}`);
  failures++;
}

/* ── An emptied gas field stays empty ───────────────────────────────────────
 * The two fee boxes are seeded with the stored caps, because the form posts both
 * and changing only the tip should not mean retyping the max. That seeding used
 * to run on any poll that found a box empty — and empty is exactly where you are
 * the instant you backspace one to retype it, so the old number went straight
 * back in under the cursor, every second and a half.
 */
try {
  page.setS({ mode: 'admin', step: 'admin', authed: true, scan_gen: 0,
              fee_max: 40, fee_prio: 3 });
  page.render();
  els.get('in_fmax').value = '';
  els.get('in_fprio').value = '5';
  page.render();
  assert.strictEqual(els.get('in_fmax').value, '',
    'a poll refilled a gas field the operator had just cleared');
  assert.strictEqual(els.get('in_fprio').value, '5',
    'a poll overwrote a retyped tip');
  console.log('  ok    an emptied gas field is not refilled by the next poll');
} catch (e) {
  console.log('  FAIL  an emptied gas field is not refilled by the next poll\n'
              + `        ${e.message}`);
  failures++;
}

/* ── Proposing an empty field ───────────────────────────────────────────────
 * The button is the same four times over and the field it reads is off-screen on
 * a phone by the time you have scrolled to it, so pressing it with nothing typed
 * has to say which box is empty and take the operator there. It used to answer
 * "Nothing to propose." in the red error style, which reads as the terminal
 * having refused the address rather than never having been given one.
 */
try {
  els.get('in_eth').value = '';
  els.get('in_eth').focused = false;
  els.get('msg').className = '';
  els.get('go_eth').onclick.call(els.get('go_eth'));
  assert.notStrictEqual(els.get('msg').className, 'err',
    'an empty box is a step not taken, not an error');
  assert.match(els.get('msg').textContent, /empty/,
    'the message should say the box is empty');
  assert.strictEqual(els.get('in_eth').focused, true,
    'the empty field should be focused, so the operator lands on it');
  console.log('  ok    proposing an empty address says which box and goes there');
} catch (e) {
  console.log(`  FAIL  proposing an empty address says which box and goes there
        ${e.message}`);
  failures++;
}

/* ── Browse opens the native picker ─────────────────────────────────────────
 * The firmware file input is hidden and this button is the only thing that can
 * open it, so a rename on one side leaves the admin page with no way to update
 * the terminal at all — and nothing else would notice, since the button is still
 * there and still does something.
 */
try {
  els.get('file').clicked = 0;
  els.get('up').onclick.call(els.get('up'));
  assert.strictEqual(els.get('file').clicked, 1,
    'Browse did not open the file picker');
  console.log('  ok    Browse opens the file picker');
} catch (e) {
  console.log(`  FAIL  Browse opens the file picker\n        ${e.message}`);
  failures++;
}

/* ── The Wi-Fi reveal eye ───────────────────────────────────────────────────
 * A venue passphrase typed blind on a phone and rejected tells the operator
 * nothing about which of the two got it wrong. Both directions, because a toggle
 * that only unmasks is a password left on a screen facing a room.
 */
try {
  const eye = els.get('eye');
  const p = els.get('wpass');
  p.type = 'password';
  eye.onclick.call(eye);
  assert.strictEqual(p.type, 'text', 'the eye did not reveal the password');
  assert.strictEqual(eye.attrs['aria-pressed'], 'true');
  eye.onclick.call(eye);
  assert.strictEqual(p.type, 'password', 'the eye did not mask it again');
  assert.strictEqual(eye.attrs['aria-pressed'], 'false');
  console.log('  ok    the reveal eye masks and unmasks the Wi-Fi password');
} catch (e) {
  console.log(`  FAIL  the reveal eye\n        ${e.message}`);
  failures++;
}

/* ── The authorisation handshake, end to end ────────────────────────────────
 * The regression test for a bug that shipped: the page polled /api/state without
 * the session token, so after the operator typed the admin code on the panel the
 * browser went on reporting authed:false forever and sat on "Authorise this
 * browser" with no way forward. Nothing in the static checks could see it — the
 * script parsed, every id was wired, render() was correct. Only driving the real
 * request sequence against a server that enforces the token catches it.
 */
const flush = async () => { for (let i = 0; i < 20; i++) { await new Promise(r => setImmediate(r)); } };

(async () => {
  srv.live = true;

  try {
    /* 1. Arrive. The page should ask to be authorised on its own — the operator
     *    should not have to press anything before the panel starts asking. */
    await page.poll();
    await flush();
    assert.strictEqual(srv.asks, 1, 'the page should ask the terminal on arrival');
    assert.strictEqual(els.get('s_auth').hidden, false, 'auth section should show');
    assert.strictEqual(els.get('waiting').hidden, false,
      'the waiting box should show once the terminal has been asked');
    console.log('  ok    arriving asks the terminal by itself');

    /* 2. The operator types the code on the panel. */
    srv.granted = true;
    srv.pending = false;

    /* 3. The very next poll must notice. This is the assertion that fails on the
     *    shipped bug: without the header the body stays minimal and authed:false. */
    await page.poll();
    await flush();
    assert.strictEqual(page.getS().authed, true,
      'the page never saw the grant — is the token being sent on /api/state?');
    assert.strictEqual(els.get('s_auth').hidden, true, 'auth section should be gone');
    for (const id of ['s_addr', 's_net', 's_ct', 's_fee', 's_wifi', 's_fw']) {
      assert.strictEqual(els.get(id).hidden, false, `${id} should be visible in admin mode`);
    }
    assert.strictEqual(els.get('cur_ssid').textContent, 'Lucky_2.4G');
    console.log('  ok    the grant is picked up on the next poll');

    /* 4. And nothing the page sends the device once it holds a token may omit it.
     *    Stated over the whole sequence rather than per call, so an endpoint added
     *    later without the header fails here too. */
    const naked = srv.seen.slice(srv.tokenAt)
      .filter(r => r.url.startsWith('/api/') && !r.token);
    assert.deepStrictEqual(naked.map(r => r.url), [],
      'these requests went out with no session token');
    console.log('  ok    every /api/ request carries the token once one exists');
  } catch (e) {
    console.log(`  FAIL  authorisation handshake\n        ${e.message}`);
    failures++;
  }

  /* ── The end of the wizard, and its one way back ────────────────────────────
   * Handing over the venue network takes the terminal's radio off the setup
   * network, so the page cannot report the outcome: it says the job is done and
   * sends the operator to the panel. Two things have to hold. Everything else
   * must be gone — a form still on screen now posts to nothing. And the screen
   * must not be permanent: the terminal answers again while the join is being
   * attempted (both networks, for a few seconds), and only a terminal with
   * something to report is the terminal actually coming back.
   */
  const wizardWifi = extra => Object.assign({
    mode: 'wizard', step: 'wifi', authed: true, auth_pending: false,
    version: '1.0.0', pay_eth: '0xAAA', pay_trx: 'TBBB',
    ct_eth: '', ct_trx: '', ssid: '', pending: '', note: '', scan_gen: 0,
  }, extra || {});

  try {
    srv.state = wizardWifi();
    await page.poll();
    await flush();
    assert.strictEqual(els.get('s_wifi').hidden, false, 'the Wi-Fi step should show');

    els.get('ssid').value = 'Venue_5G';
    els.get('wpass').value = 'hunter2hunter2';
    els.get('go_wifi').onclick.call(els.get('go_wifi'));
    await flush();
    for (const id of SECTIONS) {
      assert.strictEqual(els.get(id).hidden, id !== 's_final',
        `${id} should be ${id === 's_final' ? 'visible' : 'hidden'} once the wizard has handed over the network`);
    }
    assert.strictEqual(els.get('wpass').value, '',
      'the venue passphrase should not be left in the field');
    console.log('  ok    handing over the network ends the page');

    /* The join is in flight and the device still answers. Not a return. */
    await page.poll();
    await flush();
    assert.strictEqual(els.get('s_final').hidden, false,
      'an answer during the join attempt must not undo the finished screen');
    console.log('  ok    an answer mid-join does not undo it');

    /* It would not join. The terminal is back on the setup network with a
     * reason, and the page has to become usable again by itself. */
    srv.state = wizardWifi({ note: 'Could not join that network' });
    await page.poll();
    await flush();
    assert.strictEqual(els.get('s_final').hidden, true,
      'the terminal came back with a note and the page stayed on the finished screen');
    assert.strictEqual(els.get('s_wifi').hidden, false,
      'the Wi-Fi step should be back so the operator can try again');
    assert.strictEqual(els.get('note').textContent, 'Could not join that network');
    console.log('  ok    a failed join brings the page back with the reason');
  } catch (e) {
    console.log(`  FAIL  end of the wizard\n        ${e.message}`);
    failures++;
  }

  if (failures) {
    console.error(`portal render test: ${failures} failure(s)`);
    process.exit(1);
  }
  console.log(`portal render test OK (${CASES.length + 4} render scenarios, ` +
              'handshake, end of wizard)');
})();

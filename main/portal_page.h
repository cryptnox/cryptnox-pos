/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file portal_page.h
 * @ingroup provisioning
 * @brief The config portal's document — the HTML/CSS half and the script half.
 *
 * Lifted out of provision.cpp, which is the HTTP server: the two literals below
 * were ~700 of its 2100 lines and are read by exactly one function there
 * (page_get, which concatenates them into one chunked response). Nothing else
 * includes this, so the statics are one copy in one translation unit.
 *
 * Checked by tools/check_portal_page.py, which extracts both literals from this
 * file, verifies the script parses and that every id the script reaches for
 * exists in the markup — and emits the script for tools/test_portal_render.js.
 * Edit the page here; the checks run from scripts/checks.sh.
 */

#ifndef PORTAL_PAGE_H
#define PORTAL_PAGE_H

#include "card_front.h"   /* CARD_FRONT_PNG_URI — the portal's card picture */
#include "portal_fonts.h" /* PORTAL_FONTS_CSS — the faces, tools/gen_portal_fonts.py */

/* The AP's own address, and therefore the answer to every DNS question the
 * portal is asked. esp_netif's SoftAP default; changing it means changing this
 * string. Defined here because the page below prints the URL for an operator to
 * type by hand, and provision.cpp redirects to the same one — one definition, so
 * what is shown and what is served cannot disagree. */
#define PORTAL_IP     "192.168.4.1"
#define PORTAL_URL    "http://" PORTAL_IP "/"

/* One document for both modes and every step, with each section hidden until
 * /api/state says it applies. Rendering by toggling `hidden` rather than by
 * building DOM keeps the JavaScript to something a reviewer can read, and means
 * the wizard and the admin page cannot drift apart into two designs. */
static const char *const PAGE_HTML =
"<!doctype html><html lang=en><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Cryptnox POS</title><style>"
PORTAL_FONTS_CSS

/* The Cryptnox palette, taken from the brand's own stylesheet
 * (cryptnox.github.io/docs/source/_static/custom.css): apricot #fcb770 as the
 * accent, slate #2c3e50 as the ink, #e1e4e5 hairlines. Plus a dark scheme,
 * because this is opened on a phone in a venue and a white page at night is the
 * first thing an operator complains about. Custom properties rather than two
 * stylesheets: the dark block only restates the colours that differ.
 *
 * --accf is slate and NOT white on purpose. #fcb770 is a light accent: white on
 * it is 1.8:1 and unreadable, slate on it is 6.3:1 and passes AA. Anything put on
 * the accent from here on uses --accf. */
":root{color-scheme:light dark;accent-color:var(--acc);"
"--bg:#f4f6f8;--card:#fff;--fg:#2c3e50;--dim:#5a6874;--line:#e1e4e5;"
"--soft:#f8f9fa;--acc:#fcb770;--accf:#2c3e50;--ink:#2c3e50;"
"--tint:#fdf3e8;--tintl:#f2d3ac;--tintf:#8a5a1c;"
"--okbg:#eaf6ef;--okl:#b8e0c8;--okf:#186c39;"
"--errbg:#f8d7da;--errl:#f5c6cb;--errf:#721c24;"
"--sh:0 1px 2px rgba(44,62,80,.05),0 1px 8px rgba(44,62,80,.04)}"
"@media(prefers-color-scheme:dark){:root{"
"--bg:#131a21;--card:#1b242e;--fg:#eaeff4;--dim:#93a2b1;--line:#2a3540;"
"--soft:#222d38;--acc:#fcb770;--accf:#22303d;--ink:#0e141a;"
"--tint:#2a2318;--tintl:#4d3d26;--tintf:#fcb770;"
"--okbg:#16281e;--okl:#27492f;--okf:#7fd0a0;"
"--errbg:#2c1619;--errl:#5b2b30;--errf:#f2a2aa;"
"--sh:0 1px 2px rgba(0,0,0,.3)}}"

"*{box-sizing:border-box}"
/* Sections are toggled by the `hidden` attribute and some of them are flex or
 * grid, which outranks the browser's default `[hidden]{display:none}`. Without
 * this line every section shows at once. */
"[hidden]{display:none!important}"
/* The terminal's own faces: Inter for everything read, Plus Jakarta Sans
 * (--head) for the brand, headings and buttons — the split the panel uses. Both
 * are embedded above, because the phone is on a captive portal with no route to
 * a font CDN; the system stack is only what shows while they decode. Weights
 * are the embedded ones only (Inter 400/500/600, Jakarta 600) — asking for
 * another gets a faux bold, which is why <b> is pinned to 600. */
":root{--head:'Plus Jakarta Sans',Inter,-apple-system,'Segoe UI',system-ui,sans-serif}"
"b,strong{font-weight:600}"
"body{margin:0;padding:0 16px 40px;background:var(--bg);color:var(--fg);"
"font:16px/1.55 Inter,-apple-system,BlinkMacSystemFont,'Segoe UI',"
"system-ui,sans-serif;-webkit-font-smoothing:antialiased}"
"header,main{max-width:34rem;margin:0 auto}"
"header{display:flex;align-items:center;gap:10px;padding:22px 2px 16px}"
".mark{flex:0 0 30px;width:30px;height:30px;display:block}"
".brand{font-family:var(--head);font-size:1.05rem;font-weight:600;letter-spacing:-.015em;margin:0}"
".chip{margin-left:auto;padding:6px 10px;border-radius:999px;font-size:.75rem;"
"color:var(--dim);background:var(--card);border:1px solid var(--line);font-weight:500}"

"section{background:var(--card);border:1px solid var(--line);border-radius:16px;"
"padding:18px;margin:0 0 14px;box-shadow:var(--sh)}"
/* The apricot tick beside a heading is the only decoration on the page. It is
 * what makes a stack of grey cards read as Cryptnox rather than as a default
 * form, and it costs one pseudo-element. */
"h2{font-family:var(--head);font-size:.95rem;font-weight:600;margin:0 0 .35rem;padding-left:12px;"
"position:relative;letter-spacing:-.005em}"
"h2::before{content:'';position:absolute;left:0;top:.28em;width:3px;"
"height:.95em;border-radius:2px;background:var(--acc)}"
/* A second heading inside one card ("Or type them") is a divider, not a new card. */
"section h2~h2{margin-top:1.6rem;padding-top:1.2rem;border-top:1px solid var(--line)}"
"p,label{display:block;color:var(--dim);font-size:.92rem;margin:.3rem 0 .9rem}"
/* A label belongs to the field under it, so it keeps only the field's own 6px. */
"label{margin-bottom:0;font-weight:500}"

"input,select,button{font:inherit;width:100%;padding:12px 14px;margin:6px 0 0;"
"border:1px solid var(--line);border-radius:11px;background:var(--soft);"
"color:var(--fg)}"
/* The six address fields are the inputs with no type= — the other two are the
 * Wi-Fi password and the file picker. Addresses are compared character by
 * character against a panel, so they get a face where 0 and O differ. */
"input:not([type]){font-family:ui-monospace,SFMono-Regular,Menlo,monospace;"
"font-size:.9rem}"
"input::placeholder{color:var(--dim)}"
/* Radio rows. The shared rule above stretches every control to the card width,
 * which on a phone turns each radio into a full-width box with its own label
 * stranded on the line below it — two options that read as four things. The row
 * is the target instead: the label is the box, the radio keeps its intrinsic
 * size beside the text, and a name too long for the width wraps under itself
 * rather than under the button. */
".opt{display:flex;align-items:center;gap:12px;min-height:48px;"
"margin:8px 0 0;padding:10px 14px;border:1px solid var(--line);"
"border-radius:11px;background:var(--soft);color:var(--fg);cursor:pointer}"
"input[type=radio]{width:auto;flex:none;margin:0;padding:0;"
"accent-color:var(--acc)}"
/* The networks under each name, not trailing off the end of it. */
".opt small{display:block;margin-top:1px}"
/* Provenance under a value — "built into this firmware" and the like. Grey and a
 * size down, so the address above it stays the thing being read. */
"small{display:inline-block;margin-top:3px;color:var(--dim);font-size:.82rem}"
":focus-visible{outline:2px solid var(--acc);outline-offset:2px}"
"input[type=file]{padding:10px}"
"input[type=file]::file-selector-button{font:inherit;margin-right:10px;"
"padding:7px 12px;border:1px solid var(--line);border-radius:8px;"
"background:var(--card);color:var(--fg)}"

/* The file input is driven by the Browse button beside it, so it is taken out of
 * the flow rather than removed from it. It used to carry `hidden`, and a
 * display:none input is one iOS Safari will not open from a scripted .click() —
 * the button did nothing at all on a phone, which is the one device this page is
 * designed to be used from. Present, laid out, and invisible: clip-path over
 * width/height:0 so the element still has a box for the picker to hang off. */
".sr{position:absolute;width:1px;height:1px;padding:0;margin:-1px;border:0;"
"overflow:hidden;clip-path:inset(50%);white-space:nowrap}"

/* 48px so it is a thumb target, not a mouse one. */
"button{min-height:48px;margin-top:10px;border:0;background:var(--acc);"
"color:var(--accf);font-family:var(--head);font-weight:600;cursor:pointer;"
"transition:filter .15s,transform .05s}"
"button:hover{filter:brightness(1.06)}button:active{transform:scale(.995)}"
"button[disabled]{opacity:.45;cursor:not-allowed;filter:none}"
"button.alt{background:var(--card);color:var(--fg);border:1px solid var(--line)}"
"button.alt:hover{border-color:var(--acc);filter:none}"

/* The reveal eye sits inside the password box. The wrapper carries the field's
 * margin so the button can be centred on the input itself, and the input keeps
 * its text clear of the button. */
".pw{position:relative;margin:6px 0 0}.pw input{margin:0;padding-right:52px}"
".eye{position:absolute;right:5px;top:50%;transform:translateY(-50%);"
"width:42px;height:38px;min-height:0;margin:0;padding:0;background:none;"
"border:0;color:var(--dim)}"
".eye:hover{filter:none;color:var(--fg)}"
".eye[aria-pressed=true]{color:var(--fg)}"
".eye svg{display:block;width:22px;height:22px;margin:0 auto;fill:none;"
"stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}"
/* Which of the two is drawn is derived from aria-pressed rather than toggled in
 * JavaScript, so the state a screen reader is told and the state the icon shows
 * cannot drift apart — and .hidden does not exist on an SVG element anyway.
 * Masked shows the plain eye ("reveal"); revealed shows the struck-through one. */
".eye .off,.eye[aria-pressed=true] .on{display:none}"
".eye[aria-pressed=true] .off{display:block}"

"code{font:.85rem/1.4 ui-monospace,SFMono-Regular,Menlo,monospace;"
"background:var(--soft);border:1px solid var(--line);padding:3px 7px;"
"border-radius:7px;word-break:break-all;color:var(--fg)}"
"pre{background:var(--soft);border:1px solid var(--line);padding:12px;"
"border-radius:11px;white-space:pre-wrap;color:var(--dim);font-size:.85rem}"

/* :empty rather than a JS toggle — render() already writes '' when there is
 * nothing to say, so the banner collapses on its own. */
"#msg,#note{margin:0 0 14px;padding:12px 14px 12px 16px;border-radius:12px;"
"background:var(--card);border:1px solid var(--line);border-left:3px solid "
"var(--acc);color:var(--fg);box-shadow:var(--sh)}"
"#msg:empty,#note:empty{display:none}"
/* Severity, Bootstrap's alert colours in this palette's terms: a refused address
 * and a stored one used to be the same grey box with the same accent bar, which
 * is how an operator walks away from a rejection thinking it went in. Red or
 * green, and sticky — the message is at the top of the document and the button
 * that produced it may be a scroll away. */
"#msg{position:sticky;top:8px;z-index:5}"
/* Written with the id and not as a bare .err, which would lose to the
 * id-carrying base rule above it and repaint nothing at all. */
"#msg.err{background:var(--errbg);border-color:var(--errl);"
"border-left-color:var(--errf);color:var(--errf);font-weight:600}"
"#msg.ok{background:var(--okbg);border-color:var(--okl);"
"border-left-color:var(--okf);color:var(--okf)}"
/* The device's own line, and it is only ever written when something did not go
 * the way it was asked, so it wears the warning tint outright. */
"#note{background:var(--tint);border-color:var(--tintl);color:var(--tintf)}"
/* The card illustration — the real card front, inlined as a data URI so it costs
 * no request on a captive portal. Beside its paragraph rather than above it: the
 * picture is only there to say "this object", so it earns a thumbnail's worth of
 * a phone screen, not a banner's. The 1px halo traces the card's own rounded
 * outline (drop-shadow follows the alpha, not the box), which a black card needs
 * to read as an object on the dark scheme's near-black background. */
".cardrow{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;"
"gap:.9rem;margin:.3rem 0 .9rem}"
".cardart{flex:0 1 15rem;width:15rem;max-width:100%;height:auto;"
"filter:drop-shadow(0 0 1px var(--dim))}"
/* The wrap is the whole narrow-screen story, no media query: a phone cannot fit
 * 15rem of card and 11rem of prose on one line, so the paragraph drops below the
 * picture — which is where it sat before it moved beside it. */
".cardrow p{flex:1 1 11rem;margin:0}"

".wait{display:flex;align-items:center;gap:12px;padding:14px 16px;"
"border-radius:12px;background:var(--tint);border:1px solid var(--tintl);"
"color:var(--tintf);font-size:.92rem}"
/* The same tinted box holding a paragraph instead of a spinner and a line. Flex
 * makes every run of text its own item, so "Browse does nothing?" and the
 * sentence explaining it were laid out as two columns and squeezed against each
 * other on a phone — which is the width this particular note is always read at. */
".wait.prose{display:block}"
/* The pairing line under "Waiting for the admin code", not squeezed beside it.
 * A box's bold line gets a zero basis so it stays beside the spinner: with
 * wrapping on, a full-width line wraps under it instead of shrinking. */
".wait{flex-wrap:wrap}.wait b{flex:1 1 0}#pair{flex-basis:100%;margin:0}"
".spin{flex:0 0 18px;width:18px;height:18px;border:2px solid currentColor;"
"border-top-color:transparent;border-radius:50%;animation:sp .8s linear infinite}"
"@keyframes sp{to{transform:rotate(360deg)}}"
"@media(prefers-reduced-motion:reduce){.spin{animation:none}}"

/* The one card that is a demand rather than a form, so it wears the accent
 * outright instead of a hairline. */
"#s_pend{background:var(--tint);border-color:var(--tintl);"
"border-left:3px solid var(--acc)}"
"#s_pend h2,#s_pend p{color:var(--tintf)}"
"#s_pend h2::before{display:none}#s_pend h2{padding-left:0}"
"#pend{font-weight:600;word-break:break-all}"
/* A chosen .bin's name, straight off the operator's filesystem: it has no spaces
 * to break at, so without this a long one widens the card and takes the page's
 * horizontal scrollbar with it. */
"#fwfile{word-break:break-all}"
/* Finishing is the one green moment in the flow; ui.cpp's COL_SUCCESS. */
"#s_final{background:var(--okbg);border-color:var(--okl)}"
"#s_final h2,#s_final p{color:var(--okf)}"
"#s_final h2::before{background:var(--okf)}"
"#nav{background:none;border:0;padding:0;box-shadow:none}"
"</style></head><body>"

/* The Cryptnox mark from assets/logo.svg, inlined so it needs no request — the
 * phone is on a captive portal and a second GET for a logo is a second thing
 * that can hang. The C is white as in assets/logo.svg, not an accent that follows
 * the scheme; the disc behind it is --ink, dark in both schemes, so it stays legible.
 *
 * A <symbol> and a <use> rather than the path inline in the header: it was drawn
 * twice when the card illustration was line art too, and the indirection is kept
 * because it costs nothing and the next place that wants the mark is free. The
 * fills are inline styles and not a `.mark path{}` rule because <use> clones into
 * a shadow tree that outer selectors cannot reach; custom properties still
 * inherit into it, so var() resolves and the scheme still applies. */
"<svg width=0 height=0 aria-hidden=true style=position:absolute>"
"<symbol id=cnx viewBox='0 0 461 461'>"
"<circle cx=230.5 cy=230.5 r=230.5 style='fill:var(--ink)'/>"
"<path style=fill:#fff d='M229.02 406C205.904 406 183.016 401.434 161.66 392.565C140.304 383.694 "
"120.9 370.694 104.555 354.304C88.2102 337.914 75.2443 318.457 66.3988 297.044C57.5533 "
"275.629 53 252.678 53 229.5C53 206.322 57.5533 183.371 66.3988 161.956C75.2443 140.542 "
"88.2102 121.086 104.555 104.696C120.9 88.3063 140.304 75.305 161.66 66.4354C183.016 "
"57.5657 205.904 53 229.02 53C274.665 53 315.69 68.9362 347.641 99.0661L352.575 "
"103.828L340.286 117.652L334.964 112.575C306.523 85.724 269.878 71.5302 229.02 "
"71.5302C187.237 71.5302 147.167 88.1732 117.622 117.798C88.0774 147.424 71.4798 "
"187.604 71.4798 229.5C71.4798 271.396 88.0774 311.576 117.622 341.202C147.167 370.826 "
"187.237 387.47 229.02 387.47C271.523 387.47 305.47 370.144 328.108 353.689L300.499 "
"312.033C272.946 329.859 254.06 336.122 229.02 336.122C200.838 336.122 173.811 324.897 "
"153.883 304.915C133.956 284.933 122.761 257.832 122.761 229.574C122.761 201.315 133.956 "
"174.215 153.883 154.233C173.811 134.251 200.838 123.026 229.02 123.026C252.519 122.992 "
"275.419 130.463 294.401 144.354L305.1 131.382C283.171 114.798 256.487 105.758 229.02 "
"105.607C196.241 105.607 164.804 118.664 141.626 141.906C118.448 165.147 105.427 196.669 "
"105.427 229.537C105.427 262.405 118.448 293.927 141.626 317.169C164.804 340.41 196.241 "
"353.466 229.02 353.466C249.711 353.777 270.114 348.586 288.155 338.42L295.122 "
"334.492L305.286 350.039L297.228 354.578C276.44 366.31 252.927 372.32 229.075 "
"371.997C191.395 371.997 155.258 356.987 128.614 330.272C101.971 303.555 87.003 267.32 "
"87.003 229.537C87.003 191.754 101.971 155.519 128.614 128.803C155.258 102.086 191.395 "
"87.0768 229.075 87.0768C264.098 87.2915 297.879 100.115 324.264 123.21L331.009 "
"129.159L296.618 170.723L289.467 164.237C272.761 149.522 251.255 141.459 229.02 "
"141.574C205.739 141.574 183.412 150.848 166.95 167.354C150.489 183.861 141.241 206.249 "
"141.241 229.592C141.241 252.937 150.489 275.324 166.95 291.831C183.412 308.337 205.739 "
"317.611 229.02 317.611C252.359 317.611 269.102 311.292 297.875 291.669L305.618 "
"286.276L353 357.803L346.274 363.083C310.977 391.157 270.506 406 229.02 406Z'/>"
"</symbol></svg>"
"<header><svg class=mark aria-hidden=true><use href='#cnx'/></svg>"
"<h1 class=brand>Cryptnox POS</h1>"
"<span class=chip>fw <b id=ver>&hellip;</b></span></header>"
"<main>"
"<p id=msg role=alert></p>"
"<p id=note role=status></p>"

/* Authorisation. The only thing an unauthorised browser can see, and it does not
 * ask for the code — it asks the operator to look at the terminal. */
"<section id=s_auth hidden><h2>Authorize this browser</h2>"
"<p>The admin code is never typed here. Enter it on the terminal's own screen "
"&mdash; that is what proves you are standing in front of it.</p>"
"<div id=waiting class=wait hidden><span class=spin></span>"
"<b>Waiting for the admin code on the terminal screen&hellip;</b>"
/* The pairing code: the panel's prompt reads "Browser NNNN" for the browser it
 * is about to let in. Another phone on the access point asking first would show
 * a different number there — the one thing that tells the operator not to type
 * the code. */
"<p id=pair></p></div>"
"<button id=go_auth>Ask the terminal again</button></section>"

/* Something is waiting to be accepted on the panel. Shown over everything else,
 * because until it is resolved nothing else can be proposed. */
"<section id=s_pend hidden><h2>Check the terminal screen</h2>"
"<p><span id=pend></span> is on the terminal screen now. Compare it there, "
"character by character, and accept it on the terminal.</p></section>"

/* Production or test, for all three networks at once. Not per-network: "Ethereum
 * mainnet with Tron on Nile" is not a configuration anybody wants, it is a
 * terminal half of whose sales settle in nothing.
 *
 * Written straight through like the gas fees rather than proposed on the panel,
 * and then the terminal restarts itself — the endpoints, chain ids and token
 * contracts are resolved once at boot into the stores that are reconciled before
 * every signature, and there is no honest way to swap those under a running
 * payment. The restart is the mechanism, not an inconvenience around one.
 *
 * The stored token contracts are per-network, so switching does not carry a
 * Sepolia address onto mainnet; the payout addresses are shared, because a card's
 * address is the same account on either. */
"<section id=s_net hidden><h2>Network</h2>"
"<p>Currently <code id=cur_net>&hellip;</code>. Test networks move worthless "
"tokens and are for trying the terminal out; production settles real money.</p>"
"<label class=opt><input type=radio name=net value=main id=net_main> "
"<span>Production <small>Ethereum, Polygon, Tron</small></span></label>"
"<label class=opt><input type=radio name=net value=test id=net_test> "
"<span>Test <small>Sepolia, Amoy, Nile</small></span></label>"
"<p><b>The terminal restarts when this changes.</b> Check the asset on its Tx "
"tab afterwards &mdash; a token you set by hand is stored per network, so the "
"other one falls back to the firmware's own contract.</p>"
"<button class=alt id=go_net>Switch network</button></section>"

/* Nothing here needs doing on a working terminal, and the section has to open by
 * saying so. It used to report a contract nobody had overridden as "not set",
 * which beside a USDC selection that charges perfectly well reads as a fault —
 * and the obvious repair for a fault is to paste something over an address that
 * was already right. The terminal ships knowing these; this is the override. */
"<section id=s_ct hidden><h2>Token contracts</h2>"
"<p>Which contract the terminal calls for USDC and USDT. It already has one for "
"each, built into the firmware &mdash; <b>leave these alone unless you know the "
"deployment has moved</b>. A wrong contract moves a different asset, so a new one "
"is accepted on the terminal screen like a payout address.</p>"
"<p>ERC-20 on Ethereum &mdash; using <code id=cur_cte>&hellip;</code>"
"<br><small id=src_cte></small></p>"
"<input id=in_cte placeholder='0x...' autocapitalize=off autocomplete=off>"
"<button class=alt id=go_cte>Propose ERC-20 contract</button>"
"<p>TRC-20 on Tron &mdash; using <code id=cur_ctt>&hellip;</code>"
"<br><small id=src_ctt></small></p>"
"<input id=in_ctt placeholder='T...' autocapitalize=off autocomplete=off>"
"<button class=alt id=go_ctt>Propose TRC-20 contract</button></section>"

/* Gas caps. They used to be two +/- steppers on the terminal's Tx tab; they live
 * here because every other stored setting does, and a fee typed on a keyboard beats
 * forty taps on a resistive panel. Not proposed like an address either: a cap
 * cannot send money anywhere, so the worst a wrong one does is price a sale out of
 * a block, which the terminal reports the moment it tries. Tron is absent on
 * purpose — a transfer there is paid in bandwidth and the token's energy cap is
 * compile-time, so there is nothing to set. */
"<section id=s_fee hidden><h2>Gas fees</h2>"
"<p>What the terminal is willing to pay per unit of gas on Ethereum and Polygon, "
"in Gwei. The tip is capped by the max fee, and applies to the next sale.</p>"
"<label for=in_fmax>Max fee &mdash; currently "
"<code id=cur_fmax>&hellip;</code></label>"
"<input id=in_fmax type=number min=1 max=500 step=1 inputmode=numeric>"
"<label for=in_fprio>Priority fee (tip) &mdash; currently "
"<code id=cur_fprio>&hellip;</code></label>"
"<input id=in_fprio type=number min=1 max=500 step=1 inputmode=numeric>"
"<button class=alt id=go_fee>Save gas fees</button></section>"

/* The panel clock's time zone. A list rather than a typed number because every
 * wrong answer here is a plausible-looking one, and because the real set is not
 * the round hours people expect — India is +5:30 and Nepal +5:45.
 *
 * Regions first: each is a standard offset plus one of the DST rules in
 * civil_time.h, and the terminal changes the hour itself. Then every fixed
 * offset, for anywhere whose rules the terminal does not know. */
"<section id=s_clock hidden><h2>Clock</h2>"
"<p>What the time in the corner of the terminal's screen reads in &mdash; "
"currently <code id=cur_tz>&hellip;</code>. The terminal keeps UTC from the "
"network; this is only what it adds before showing it.</p>"
"<label for=in_tz>Time zone</label>"
"<select id=in_tz></select>"
"<p><small>A region follows its daylight-saving changes by itself. A fixed "
"offset never changes, so where the clocks move you come back here twice a "
"year.</small></p>"
"<button class=alt id=go_clock>Save clock</button></section>"

/* One section, two ways in. Reading a card and typing an address answer the same
 * question — where do takings go — so they were two cards headed "Card addresses"
 * and "Send to", which made the operator choose between two settings before
 * finding out they were one. Card route first (it is the one that cannot be
 * mistyped), typing under a divider heading; either way the terminal only
 * *proposes*, and somebody accepts it on the panel. */
"<section id=s_addr hidden><h2>Payout addresses</h2>"
"<p>Where takings are sent &mdash; the merchant's own address. Either way this "
"only <i>proposes</i> it: the terminal shows it on its own screen and somebody "
"has to accept it there.</p>"
/* Two addresses, three networks. Polygon is EVM — same card key, same derivation
 * path, same 0x address — so it spends the Ethereum one, and saying so here is
 * what stops an operator hunting for a Polygon field that will never exist and
 * concluding the terminal cannot take Polygon payments. */
"<p>Ethereum &mdash; also used for Polygon &mdash; currently "
"<code id=cur_eth>&hellip;</code><br>"
"Tron &mdash; currently <code id=cur_trx>&hellip;</code></p>"
/* Which card, and what to do with it. The instruction below says "tap a Cryptnox
 * card" to somebody who may never have seen one, so show them the actual card
 * front (assets/cryptnox_card.png, inlined by tools/gen_card_front.py) rather than a
 * line drawing of a generic one — the thing in their hand is what they have to
 * recognise. Decorative: the paragraph beside it says everything the picture does. */
"<div class=cardrow><img class=cardart src='" CARD_FRONT_PNG_URI "' alt=''>"
"<p>Whichever card is tapped, <i>its own</i> addresses become the ones takings "
"are sent to &mdash; so tap the merchant's card, not a customer's. The terminal "
"asks for that card's PIN, then shows each address for acceptance.</p></div>"
"<button class=alt id=go_card>Read from a Cryptnox card</button>"
"<h2>Or type an address</h2>"
"<p>Ethereum <i>(and Polygon &mdash; one address serves both)</i></p>"
"<input id=in_eth placeholder='0x...' autocapitalize=off autocomplete=off>"
"<button id=go_eth>Propose Ethereum address</button>"
"<p>Tron</p>"
"<input id=in_trx placeholder='T...' autocapitalize=off autocomplete=off>"
"<button id=go_trx>Propose Tron address</button></section>"

"<section id=s_wifi hidden><h2>Wi-Fi</h2>"
"<p id=wifi_note></p>"
"<p>Currently <code id=cur_ssid>&hellip;</code></p>"
"<label for=ssid>SSID:</label>"
"<select id=ssid></select>"
/* The eye is the panel's (ui.cpp's Wi-Fi keyboard has one): a venue passphrase
 * typed blind on a phone and rejected tells the operator nothing about which of
 * the two got it wrong. */
"<label for=wpass>Password:</label>"
"<div class=pw><input id=wpass type=password autocomplete=off>"
"<button type=button class=eye id=eye aria-label='Show password' "
"aria-pressed=false>"
/* Feather's eye / eye-off (MIT), which Lucide, Heroicons and every phone keyboard's
 * own reveal button all draw a version of — the shape people already know. Inline
 * paths rather than a glyph: an emoji renders as a different picture on every
 * handset (and in colour on some), and a font or an <img> would be a second request
 * on a captive portal that has nowhere to fetch it from. Stroked in currentColor,
 * so it follows the colour scheme for free. */
"<svg class=on viewBox='0 0 24 24' aria-hidden=true>"
"<path d='M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z'/>"
"<circle cx=12 cy=12 r='3'/></svg>"
"<svg class=off viewBox='0 0 24 24' aria-hidden=true>"
"<path d='M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 "
"5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 "
"3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24'/>"
"<path d='M1 1l22 22'/></svg>"
"</button></div>"
"<button id=go_wifi>Join this network</button>"
"<button class=alt id=go_rescan>Scan again</button></section>"

/* No "check for updates" button: this page is served on the terminal's own
 * SoftAP with nothing behind it, so the browser reading it cannot reach a
 * release list. Download the image on something that has internet, bring it
 * here. The native file input is hidden behind our own button so the control
 * reads like the rest of the page — and it says Browse, because picking the
 * file is all it does; the terminal's own screen is where it gets installed. */
"<section id=s_fw hidden><h2>Firmware</h2>"
"<p>Download the signed <code>.bin</code> on a device that has internet, then "
"hand it to the terminal here &mdash; the terminal never connects to the "
"internet for this. It checks the signature itself, and nothing is installed "
"until somebody accepts the version on its screen.</p>"
/* No accept= filter. It said '.bin', and iOS maps an accept list to file types it
 * knows — an extension it does not recognise leaves every file in the picker
 * greyed out, so the operator gets a file browser that will not let them pick the
 * file they came to send. The terminal is the wrong place to be lax about what it
 * accepts and it is not being lax: the image is SHA-256'd and signature-checked
 * before anything is staged, and a version has to be accepted on the panel. A
 * filename filter never protected any of that. */
"<input type=file id=file class=sr>"
"<button class=alt id=up>Browse</button>"
"<p id=fwfile></p>"

/* The one thing on this page that a captive portal cannot do.
 *
 * Joining the terminal's AP makes the phone pop this page up by itself, in the
 * Wi-Fi sign-in window — Android's CaptivePortalLogin activity, iOS's equivalent
 * sheet. That window is a cut-down WebView, and neither platform wires up a file
 * chooser in it: the input is there, the tap reaches it, and nothing opens. No
 * markup fixes that, because the missing piece is on the other side of the
 * WebView. Everything else on this page works there, which is precisely why it
 * reads as the button being broken rather than as the window being the wrong one.
 *
 * So the section says so, in the place the operator is standing when it happens,
 * and gives them the address to open in a real browser. Shown to everybody rather
 * than sniffed for from the user agent: the WebView markers are undocumented and
 * change, and a sentence that is merely unnecessary on a laptop is cheaper than a
 * detection that is silently wrong on a handset. */
"<div class='wait prose'><b>Browse does nothing?</b> You are in the Wi-Fi sign-in "
"window your phone opened, and neither Android nor iOS lets that window pick "
"files. Stay on this network, open <code>" PORTAL_URL "</code> in your normal "
"browser, and use the Firmware section there.</div></section>"

/* Where the wizard ends, and the last thing this phone will be shown: joining
 * the venue network moves the terminal's radio off this setup network, so the
 * page is about to lose the device it is talking to. Said as a finished job
 * rather than as a dropped connection — and it replaces the whole page, because
 * every form behind it is now addressed to something that is not there.
 *
 * It is not the last word, though: if the join fails the terminal comes back on
 * this same setup network, the phone rejoins it by itself, and the poll below
 * puts the Wi-Fi step back with the reason on it. */
"<section id=s_final hidden><h2>Configuration complete</h2>"
"<p>Please follow the instructions on the terminal's screen.</p>"
"<div class=wait><span class=spin></span>"
"<b>You can close this page. If the terminal could not join that network it "
"will reopen this one, and this page will come back by itself.</b></div>"
"</section>"

"<section id=nav hidden><button id=go_next>Continue</button></section>"
"</main>";

/* Split so neither literal is unreasonable to read, and so the CSS/HTML above can
 * be edited without scrolling past the script. Concatenated by page_get(). */
static const char *const PAGE_JS =
"<script>"
"var $=function(i){return document.getElementById(i)};"
"var T='',S={},G=-1,asked=false,fin=false,seeded=false;"
/* The token survives a reload in sessionStorage: the terminal only hands it to
 * the browser that holds it, so a reload that forgot it would be locked out. */
"try{T=sessionStorage.getItem('pt')||''}catch(e){}"
"function keep(t){T=t;try{if(t)sessionStorage.setItem('pt',t);"
"else sessionStorage.removeItem('pt')}catch(e){}}"
"function pc(t){return('000'+(parseInt(t.substr(0,4),16)%10000)).slice(-4)}"
/* Every section render() can show, so the finished screen can be made exclusive
 * by construction rather than by adding `&&!fin` to every show() line — and
 * so a section added later without a thought for the end of the wizard is hidden
 * there rather than left on screen addressed to a terminal that has gone. */
"var SEC=['s_auth','waiting','s_pend','s_addr','s_net','s_ct','s_fee','s_clock',"
"'s_wifi','s_fw','nav'];"
/* Three kinds, because "that is not a valid Ethereum address" and "it is stored"
 * in the same grey box is how a refusal gets read as a success. 'err' is the one
 * that matters, so it is what a bare say() rejection handler produces: every
 * post() failure path is an error, and none of them has to remember to say so. */
"var say=function(t,k){var m=$('msg');m.textContent=t||'';"
"m.className=t?(k||'err'):''};"
"var info=function(t){say(t,'info')};"
"var good=function(t){say(t,'ok')};"

/* Every mutating call carries the session token. A 401 means the portal was
 * restarted or the window closed, so drop the token and let the render fall back
 * to the authorise section rather than looping on a dead session. */
"function post(u,b){var h={'X-Prov-Token':T};"
"if(b!==undefined)h['Content-Type']='application/x-www-form-urlencoded';"
"return fetch(u,{method:'POST',headers:h,body:b}).then(function(r){"
"return r.text().then(function(t){"
"if(r.status==401){keep('');S.authed=false;asked=false}"
"if(!r.ok)throw (t||('HTTP '+r.status));return t})})}"

"function enc(o){var a=[];for(var k in o)"
"a.push(k+'='+encodeURIComponent(o[k]));return a.join('&')}"

"function show(i,on){$(i).hidden=!on}"

"function render(){"
"var w=(S.mode=='wizard'),st=S.step||'idle',a=!!S.authed,p=!!S.pending;"
/* Shown with a 'v', like the panel's About tab. The JSON stays bare so nothing
 * parsing /api/state has to strip it — see ota_version_display(). */
"$('ver').textContent=S.version?'v'+S.version:'?';"
"$('note').textContent=S.note||'';"
"show('s_final',fin);"
"if(fin){SEC.forEach(function(i){show(i,false)});return}"
"show('s_auth',!a);show('waiting',!a&&!!S.auth_pending);"
"$('pair').textContent=T?'Check the terminal shows Browser '+pc(T)+', then enter "
"the code there.':'';"
"show('s_pend',a&&p);"
/* In the wizard one section at a time, in the order of the flow. In admin mode
 * everything at once — it is a settings page, not a sequence. */
"show('s_addr',a&&!p&&(w?st=='addr':true));"
/* Admin mode only, like the contracts and the fees. During the wizard the
 * terminal is being set up on whatever network it shipped configured for, and a
 * switch there would restart the device mid-setup — out of the wizard, and away
 * from the phone that was walking somebody through it. */
"show('s_net', a&&!p&&!w);"
"show('s_ct',  a&&!p&&!w);"
"show('s_fee', a&&!p&&!w);"
"show('s_clock',a&&!p&&!w);"
"show('s_wifi',a&&!p&&(w?st=='wifi':true));"
"show('s_fw',  a&&!p&&!w);"
/* Continue exists to leave the address step. There is nothing after the Wi-Fi one
 * to continue to — joining is what ends the wizard — so it is not offered there. */
"show('nav',   a&&!p&&w&&st=='addr');"
"$('wifi_note').textContent=w?'This is the last step here: the terminal has one "
"radio, so joining your network drops this setup network, and the rest is on the "
"terminal screen.':'Scanning briefly interrupts this page - it comes "
"back. Changing network drops it for good, on the old address.';"
"if(a){$('cur_eth').textContent=S.pay_eth||'not set';"
"$('cur_trx').textContent=S.pay_trx||'not set';"
/* The contract in use, and where it came from on the line under it. Two elements
 * rather than one string, so "built into this firmware" is small grey prose and
 * the address stays the monospace thing you compare against the panel. */
"$('cur_cte').textContent=S.ct_eth||'none configured';"
"$('cur_ctt').textContent=S.ct_trx||'none configured';"
"$('src_cte').textContent=S.ct_eth?(S.ct_eth_own?'Set by an operator on this "
"terminal.':'Built into this firmware - nothing to do.'):'This firmware names no "
"contract for it, so that asset is refused.';"
"$('src_ctt').textContent=S.ct_trx?(S.ct_trx_own?'Set by an operator on this "
"terminal.':'Built into this firmware - nothing to do.'):'This firmware names no "
"contract for it, so that asset is refused.';"
"$('cur_net').textContent=S.mainnet?'production':'test networks';"
/* Checked from the device once, then left alone — the poll runs every couple of
 * seconds and would otherwise put the radio back under whoever had just moved it. */
"if(!$('net_main').checked&&!$('net_test').checked)"
"$(S.mainnet?'net_main':'net_test').checked=true;"
"$('cur_fmax').textContent=S.fee_max;"
"$('cur_fprio').textContent=S.fee_prio;"
/* Seeded once, on the first state that carries them, and never written again:
 * the poll runs every couple of seconds, and writing these every tick would take
 * a digit out from under whoever is typing. Keyed on a flag rather than on the
 * field being empty, because empty is where you are the instant you backspace
 * one to retype it — that put the old number straight back, mid-edit. */
"$('cur_tz').textContent=tzname(S.tz_off|0,S.tz_dst|0);"
/* Seeded with the fees and on the same flag, for the same reason: the poll runs
 * every couple of seconds, and writing the select every tick would put the
 * stored offset back under an operator who had just picked a different one and
 * not yet saved it. */
"if(!seeded&&S.fee_max){seeded=true;"
"$('in_fmax').value=S.fee_max;$('in_fprio').value=S.fee_prio;"
"$('in_tz').value=(S.tz_off|0)+','+(S.tz_dst|0)}"
"$('cur_ssid').textContent=S.ssid||'not set';"
"$('pend').textContent=S.pending||'';"
"if(S.scan_gen!==G){G=S.scan_gen;scan()}}}"

/* The device's own scan, not the browser's — a browser cannot see Wi-Fi at all.
 * Rebuilt with textContent per option so an SSID cannot inject markup. */
"function scan(){fetch('/api/scan',{headers:{'X-Prov-Token':T},"
"cache:'no-store'}).then(function(r){return r.json()}).then(function(j){"
"var s=$('ssid');s.textContent='';"
"if(!j.aps||!j.aps.length){var o=document.createElement('option');"
"o.textContent='No networks found';o.disabled=true;s.appendChild(o);return}"
"j.aps.forEach(function(n){var o=document.createElement('option');"
"o.value=n.ssid;o.textContent=n.ssid+'  ('+n.rssi+' dBm)'+(n.open?'  open':'');"
"s.appendChild(o)})}).catch(function(){})}"

/* Asking is automatic on arrival, so the panel is already demanding the code by
 * the time the operator looks up from the phone — "connect, then type it on the
 * terminal" with nothing in between. The button is only there to try again after a
 * refusal. Once per page load: a re-ask mints a new token and would throw away a
 * session somebody had already been granted. */
/* The token goes on this one too, and it is not optional: /api/state answers an
 * unauthorised request with a deliberately minimal body, so a poll without the
 * token reports authed:false forever — the panel takes the admin code, grants the
 * session, and the page sits on "Authorize this browser" with no way out. */
"function poll(){fetch('/api/state',{headers:{'X-Prov-Token':T},"
"cache:'no-store'})"
".then(function(r){return r.json()}).then(function(j){S=j;"
/* The poll that keeps running after the wizard's last screen is what waits for
 * the device to come back. It answers again while the join is being attempted —
 * the radio serves both networks for those few seconds — so an answer alone is
 * not the terminal returning. Something to report is: the terminal writes a note
 * when the network would not take it, and nothing else brings the wizard back to
 * a step it can act on. Either way the operator loses nothing by looking at the
 * panel, which is what the final screen tells them to do. */
"if(fin&&(S.note||(S.step&&S.step!='wifi')))fin=false;"
"render();"
"if(!S.authed&&!S.auth_pending&&!asked)ask()})"
".catch(function(){})}"

"function ask(){asked=true;say('');"
"post('/api/auth').then(function(t){keep(t);S.auth_pending=!S.authed;render()},"
"function(e){asked=false;say(e)})}"
"$('go_auth').onclick=ask;"

"$('go_card').onclick=function(){"
"post('/api/card').then(function(){info('Tap your Cryptnox card on the terminal "
"when it asks, then accept each address on its screen.')},say)};"

/* An empty field is not a failure, it is a step not taken yet — so the answer says
 * what to do next instead of reporting that nothing happened. "Nothing to
 * propose." was read as the terminal refusing the address rather than as never
 * having been given one, and there is no way to tell those apart from a red line
 * that describes the page's own internal state. Named per field, because this is
 * the same button four times over and the operator has to know which one. */
"function propose(u,net,el){var v=$(el).value.trim();"
"if(!v){info('That box is empty - type the address into it first.');"
"$(el).focus();return}"
"post(u,enc({net:net,addr:v})).then(function(m){$(el).value='';good(m)},say)}"
"$('go_eth').onclick=function(){propose('/api/payout','eth','in_eth')};"
"$('go_trx').onclick=function(){propose('/api/payout','tron','in_trx')};"
"$('go_cte').onclick=function(){propose('/api/contract','eth','in_cte')};"
"$('go_ctt').onclick=function(){propose('/api/contract','tron','in_ctt')};"

/* The device reboots on this one, so the answer is the last thing this page will
 * hear from it: stop the poll and say so, rather than leaving the operator
 * watching a page that quietly stops updating. */
"$('go_net').onclick=function(){"
"var m=$('net_main').checked;"
"if(m==!!S.mainnet){info('Already on that network.');return}"
"post('/api/network',enc({net:m?'main':'test'})).then(function(t){"
"clearInterval(PT);good(t)},say)};"

"$('go_fee').onclick=function(){"
"post('/api/fees',enc({max:$('in_fmax').value,prio:$('in_fprio').value}))"
".then(good,say)};"

/* Every offset the world actually uses, in minutes east of UTC. A list rather
 * than a loop over whole hours: the quarter- and half-hour zones are real, and
 * a terminal in Kathmandu offered +5:00 is a terminal that is 45 minutes wrong.
 * Filled in by script rather than written out as 38 <option> tags, which is
 * about a kilobyte of page for the same result — and the page is flash. */
"var TZ=[-720,-660,-600,-570,-540,-480,-420,-360,-300,-240,-210,-180,-120,-60,"
"0,60,120,180,210,240,270,300,330,345,360,390,420,480,525,540,570,600,630,660,"
"720,765,780,840];"
"function tzlabel(m){var s=m<0?'-':'+',a=m<0?-m:m;"
"return'UTC'+s+('0'+Math.floor(a/60)).slice(-2)+':'+('0'+(a%60)).slice(-2)}"
/* [standard offset, DST rule, places]: one row per zone, so the list is one
 * line per time the clock can show. Rule 1 EU, 2 US/Canada, 3 SE Australia,
 * 4 New Zealand, 0 none (civil_dst_t). Places with other DST rules (Chile,
 * Paraguay, Israel, Egypt, Morocco...) are left to the bare offsets. */
"var REG=["
"[-600,0,'Honolulu'],[-540,2,'Anchorage'],"
"[-480,2,'Los Angeles, San Francisco, Seattle, Vancouver, Tijuana'],"
"[-420,2,'Denver, Salt Lake City, Calgary, Edmonton'],[-420,0,'Phoenix'],"
"[-360,2,'Chicago, Dallas, Houston, Winnipeg'],"
"[-360,0,'Mexico City, Guadalajara, Regina, Guatemala, San Salvador, San Jose'],"
"[-300,2,'New York, Washington, Miami, Toronto, Montreal, Ottawa, Nassau, Port-au-Prince'],"
"[-300,0,'Bogota, Lima, Quito, Panama, Kingston, Cancun'],"
"[-240,2,'Halifax, Bermuda'],[-240,0,'San Juan, Santo Domingo, Caracas, La Paz'],"
"[-210,2,'Newfoundland'],"
"[-180,0,'Sao Paulo, Rio de Janeiro, Brasilia, Buenos Aires, Montevideo'],"
"[0,1,'London, Dublin, Lisbon, Canary Islands'],[0,0,'Reykjavik, Accra, Dakar, Abidjan'],"
"[60,1,'Zurich, Geneva, Paris, Berlin, Frankfurt, Rome, Milan, Madrid, Barcelona, "
"Amsterdam, Brussels, Luxembourg, Vienna, Vaduz, Monaco, Copenhagen, Oslo, Stockholm, "
"Warsaw, Prague, Bratislava, Budapest, Ljubljana, Zagreb, Belgrade, Sarajevo, "
"Podgorica, Skopje, Tirana, Valletta'],"
"[60,0,'Lagos, Algiers, Tunis'],"
"[120,1,'Helsinki, Tallinn, Riga, Vilnius, Athens, Sofia, Bucharest, Nicosia, Kyiv'],"
"[120,0,'Johannesburg, Cape Town, Harare, Kigali'],"
"[180,0,'Istanbul, Moscow, Minsk, Riyadh, Doha, Kuwait, Manama, Baghdad, Nairobi, "
"Addis Ababa'],"
"[210,0,'Tehran'],[240,0,'Dubai, Abu Dhabi, Muscat, Baku, Tbilisi, Yerevan, Mauritius'],"
"[270,0,'Kabul'],[300,0,'Karachi, Lahore, Tashkent, Almaty, Astana, Maldives'],"
"[330,0,'Mumbai, Delhi, Bangalore, Colombo'],[345,0,'Kathmandu'],[360,0,'Dhaka'],"
"[390,0,'Yangon'],[420,0,'Bangkok, Hanoi, Ho Chi Minh City, Jakarta, Phnom Penh'],"
"[480,0,'Beijing, Shanghai, Hong Kong, Macau, Taipei, Singapore, Kuala Lumpur, "
"Manila, Bali, Perth'],"
"[540,0,'Tokyo, Osaka, Seoul'],[570,3,'Adelaide'],[570,0,'Darwin'],"
"[600,3,'Sydney, Canberra, Melbourne, Hobart'],[600,0,'Brisbane, Guam, Port Moresby'],"
"[720,4,'Auckland, Wellington'],[720,0,'Fiji']];"
/* Every offset with no row of its own gets a bare one, so the list still
 * reaches every zone the terminal can show. Sorted by offset, DST row first. */
"TZ.forEach(function(m){if(!REG.some(function(r){return r[0]==m&&!r[1]}))REG.push([m,0,''])});"
"REG.sort(function(a,b){return a[0]-b[0]||b[1]-a[1]});"
"var DSTN=['','EU DST','US DST','AU DST','NZ DST'];"
"function tzrow(r){return tzlabel(r[0])+(r[1]?' '+DSTN[r[1]]:'')+(r[2]?' - '+r[2]:'')}"
"function tzname(m,d){for(var i=0;i<REG.length;i++)"
"if(REG[i][0]==m&&REG[i][1]==d)return tzrow(REG[i]);return tzlabel(m)}"
"REG.forEach(function(r){var o=document.createElement('option');"
"o.value=r[0]+','+r[1];o.textContent=tzrow(r);$('in_tz').appendChild(o)});"

"$('go_clock').onclick=function(){var z=$('in_tz').value.split(',');"
"post('/api/clock',enc({off:z[0],dst:z[1]})).then(good,say)};"

"$('eye').onclick=function(){var p=$('wpass'),r=(p.type=='password');"
"p.type=r?'text':'password';this.setAttribute('aria-pressed',r);"
"this.setAttribute('aria-label',r?'Hide password':'Show password')};"

"$('go_wifi').onclick=function(){var s=$('ssid').value;"
"if(!s){say('Pick a network first.');return}"
"post('/api/wifi',enc({ssid:s,pass:$('wpass').value})).then(function(m){"
"$('wpass').value='';"
/* In the wizard, handing over the network is the end of this page's job: the
 * terminal moves its radio to that network and this setup network goes with it.
 * So the page stops being a form and becomes a finished screen, rather than
 * leaving the operator on a Wi-Fi step whose buttons now reach nothing. In admin
 * mode the terminal is only changing networks, so the ordinary message stands. */
"if(S.mode=='wizard'){say('');fin=true;render()}else{good(m)}},say)};"
"$('go_rescan').onclick=function(){info('Scanning\\u2026');"
"post('/api/rescan').then(function(){},say)};"
"$('go_next').onclick=function(){post('/api/next').then(function(){},say)};"

/* XHR, not fetch: this is the leg that can stall with the flash half written,
 * and only XHR reports upload progress. */
"function send(buf){return new Promise(function(res,rej){"
"var x=new XMLHttpRequest(),sent=false;x.open('POST','/api/ota');"
"x.setRequestHeader('Content-Type','application/octet-stream');"
"x.setRequestHeader('X-Prov-Token',T);"
"x.upload.onprogress=function(e){if(e.lengthComputable)"
"info('Sending to the terminal: '+Math.round(e.loaded/e.total*100)+'%')};"
"x.upload.onload=function(){sent=true};"
"x.onload=function(){x.status==200?res(x.responseText):"
"rej(x.responseText||('HTTP '+x.status))};"
/* Two different events wearing one word. A drop with bytes still to send really
 * did lose the image. A drop after the last byte went out lost only the answer —
 * the terminal has the whole file and is verifying it, and saying "not installed"
 * about a version that is at that moment on the panel waiting to be accepted is
 * how an operator uploads the same firmware three times. */
"x.onerror=function(){rej(sent?'the file went across but the terminal did not "
"answer - if a version is showing on its screen it arrived, accept it there'"
":'the connection to the terminal dropped')};"
"x.send(buf)})}"

/* The button drives the hidden native picker, and picking is what starts the
 * transfer — there is no second "install this file" step, because nothing this
 * page does installs anything: the terminal verifies the signature and the
 * version is accepted on its own screen. A confirm button in front of that would
 * guard a transfer, not an installation. */
"$('up').onclick=function(){$('file').click()};"
/* The poll stops for the transfer. Every tick is another connection, the terminal
 * has a handful of sockets, and the upload is the one request that must not be the
 * casualty when they run out (see prov_start). Nothing is lost by pausing: the
 * progress line comes from the XHR itself, and the poll resumes in time to show
 * whatever the panel decides. */
"$('file').onchange=function(){var el=this,f=el.files[0];if(!f)return;"
"$('fwfile').textContent=f.name;$('up').disabled=true;clearInterval(PT);"
"f.arrayBuffer().then(send).then(good,function(e){say('Not installed: '+e)})"
/* Clear it, or picking the same file again is not a change event and the button
 * looks broken on a retry. */
".then(function(){$('up').disabled=false;el.value='';PT=setInterval(poll,1500)})};"

"var PT=setInterval(poll,1500);poll();"
"</script></body></html>";

#endif /* PORTAL_PAGE_H */

/*
 * Offline test of ui_chain.js against the REAL param_pages library from the
 * Schwung repo, with the host globals mocked. Proves the binding loads, the
 * grid plans our pages from the DSP's ui_pages/chain_params, renders without
 * throwing, and the pad gestures do what the help text says.
 *
 * It also cross-checks the pad tables in ui_chain.js against the ones in
 * sc808_plugin.cpp. Those two files independently decide what pad 87 is, and
 * nothing at runtime would notice them disagreeing — the drum would sound and
 * the WRONG page would open, which is the kind of bug that survives to a
 * release because it looks like a UI quirk.
 *
 *   node test/ui_chain.test.mjs [path/to/schwung/src/shared]
 */
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
let fails = 0;

/*
 * Schwung's shared param_pages library — the real one, not a stub. Tested
 * against the real thing on purpose: a mock would happily accept a page
 * hierarchy the device rejects.
 *
 * Found by argument, then $SCHWUNG_SHARED, then by looking next to this
 * checkout — which is where it lives on both the Mac and the build host, and
 * hardcoding one of those two paths is why this used to fail on the other.
 */
function findShared() {
  if (process.argv[2]) return process.argv[2];
  if (process.env.SCHWUNG_SHARED) return process.env.SCHWUNG_SHARED;
  const parent = path.dirname(ROOT);
  for (const d of fs.readdirSync(parent, { withFileTypes: true })) {
    if (!d.isDirectory()) continue;
    const p = path.join(parent, d.name, "src/shared");
    if (fs.existsSync(path.join(p, "param_pages/page_controller.mjs"))) return p;
  }
  return null;
}
const SHARED = findShared();
const check = (c, m) => { console.log((c ? "ok  : " : "FAIL: ") + m); if (!c) fails++; };

/* the DSP's two payloads, straight from the generated header */
const hdr = fs.readFileSync(path.join(ROOT, "src/dsp/sc808_params.h"), "utf8");
function cstr(name) {
  const i = hdr.indexOf(name + "[] =");
  const body = hdr.slice(i, hdr.indexOf(";", i));
  return body.split("\n").slice(1).map(l => l.trim()).filter(l => l.startsWith('"'))
    .map(l => JSON.parse(l)).join("");
}
const CHAIN_PARAMS = cstr("sc808_chain_params_json");
const UI_PAGES = cstr("sc808_ui_pages_json");
JSON.parse(CHAIN_PARAMS); JSON.parse(UI_PAGES);

/* ---- the two files must agree about the pads ------------------------- */
{
  const cpp = fs.readFileSync(path.join(ROOT, "src/dsp/sc808_plugin.cpp"), "utf8");
  const js  = fs.readFileSync(path.join(ROOT, "src/ui_chain.js"), "utf8");

  /* the DSP: pad_to_voice()'s case labels, note -> SC808_XX */
  const body = cpp.slice(cpp.indexOf("static int pad_to_voice"),
                         cpp.indexOf("static int drumrack_to_voice"));
  const dsp = {};
  for (const m of body.matchAll(/case\s+(\d+):\s*return\s+SC808_([A-Z]+)\s*;/g))
    dsp[m[1]] = m[2].toLowerCase();

  /* the editor: PAD2LEVEL, note -> page id */
  const tbl = js.slice(js.indexOf("var PAD2LEVEL"), js.indexOf("var PAD2LANE"));
  const ui = {};
  for (const m of tbl.matchAll(/(\d+):\s*"([a-z]+)"/g)) ui[m[1]] = m[2];

  const dspPads = Object.keys(dsp).sort();
  /* the editor has one extra: the Master pad, which sounds nothing */
  const uiPads = Object.keys(ui).filter(k => ui[k] !== "root").sort();
  check(JSON.stringify(dspPads) === JSON.stringify(uiPads),
        "ui_chain.js and sc808_plugin.cpp cover the same pads");
  let mismatch = null;
  for (const p of dspPads) if (dsp[p] !== ui[p]) mismatch = `${p}: dsp ${dsp[p]} vs ui ${ui[p]}`;
  check(!mismatch, "every pad maps to the same voice in both" + (mismatch ? " — " + mismatch : ""));

  /* PAD2LANE must be the DSP's enum order, which is the order of the page
   * ids in the generated hierarchy. */
  const lanesTbl = js.slice(js.indexOf("var PAD2LANE"), js.indexOf("var LEVEL2LANE"));
  const lane = {};
  for (const m of lanesTbl.matchAll(/(\d+):\s*(\d+)/g)) lane[m[1]] = Number(m[2]);
  const order = ["bd","sd","lt","mt","ht","lc","mc","hc",
                 "rs","cl","ma","cp","cb","ch","oh","cy"];
  let bad = null;
  for (const p of dspPads) if (order[lane[p]] !== dsp[p]) bad = `pad ${p}`;
  check(!bad, "PAD2LANE indices match the DSP's lane order" + (bad ? " — " + bad : ""));
}

/* ---- the remote panel's parameter keys must all exist -----------------
 *
 * web_ui.html names its keys as string literals and as `id + "_suffix"`. A
 * typo in either is a knob that draws, drags, and writes to a key the DSP has
 * never heard of — silent, and invisible until somebody notices that control
 * does nothing. Nothing else checks this, so it is checked here.
 */
{
  const html = fs.readFileSync(path.join(ROOT, "src/web_ui.html"), "utf8");
  const known = new Set(JSON.parse(CHAIN_PARAMS).map(p => p.key));
  /* keys the panel uses that are plugin-level, not chain_params rows */
  const plugin = new Set(["mute_ms"]);

  /* the lane ids the panel builds its templated keys from */
  /* Stop at PANEL, not at LANE_MASK: PANEL is a bare list of lane ids and the
   * extras regex below would read its entries as parameter keys. */
  const lanesSrc = html.slice(html.indexOf("var LANES = ["), html.indexOf("var PANEL"));
  const ids = [...lanesSrc.matchAll(/id:\s*"([a-z]+)"/g)].map(m => m[1]);
  check(ids.length === 16, `panel declares 16 lanes (got ${ids.length})`);

  /* `id + "_suffix"` call sites.
   *
   * Not every suffix belongs to every lane any more: the kick is DRY, so it
   * declares no sends. The panel names that exception in DRY_LANES and this
   * reads it from there rather than hardcoding a second copy — and then
   * asserts the exempted keys really are absent, so the exemption cannot
   * quietly cover a control that should exist. */
  const dry = new Set([...(html.match(/var DRY_LANES = \[([^\]]*)\]/) || [,""])[1]
                        .matchAll(/"([a-z]+)"/g)].map(m => m[1]));
  const SEND_SUFFIXES = new Set(["_rev", "_dly"]);
  const suffixes = [...html.matchAll(/id \+ "(_[a-z_]+)"/g)].map(m => m[1]);
  const missing = [];
  for (const id of ids)
    for (const suf of new Set(suffixes)) {
      if (dry.has(id) && SEND_SUFFIXES.has(suf)) continue;
      if (!known.has(id + suf)) missing.push(id + suf);
    }
  check(dry.size > 0, `the panel names its dry lanes (${[...dry].join(",")})`);
  const strays = [...dry].flatMap(id =>
      [...SEND_SUFFIXES].filter(suf => known.has(id + suf)).map(suf => id + suf));
  check(strays.length === 0,
        "a dry lane really has no sends in chain_params" +
        (strays.length ? " — found " + strays.join(", ") : ""));

  /* bare string literals passed to knob()/sel() as the key */
  for (const m of html.matchAll(/(?:knob|sel)\((?:top|master|voiceRow|parent),\s*"([a-z_0-9]+)"/g))
    if (!known.has(m[1]) && !plugin.has(m[1])) missing.push(m[1]);
  /* keys read or written directly through the bridge */
  for (const m of html.matchAll(/key\("([a-z_0-9]+)"\)/g))
    if (!known.has(m[1]) && !plugin.has(m[1])) missing.push(m[1]);
  /* the `extras` tables */
  for (const m of lanesSrc.matchAll(/\["([a-z_0-9]+)"\s*,\s*"/g))
    if (!known.has(m[1])) missing.push(m[1]);

  check(missing.length === 0,
        "every key web_ui.html touches exists in chain_params" +
        (missing.length ? " — missing: " + [...new Set(missing)].join(", ") : ""));

  /* LANES must be in the DSP's order, because its INDEX is the mute bit and
   * the ui_focus value. */
  const order = ["bd","sd","lt","mt","ht","lc","mc","hc",
                 "rs","cl","ma","cp","cb","ch","oh","cy"];
  check(JSON.stringify(ids) === JSON.stringify(order),
        "panel LANES is in the DSP's enum order (mute bits and ui_focus depend on it)");

  /* PANEL is the DRAW order — a real 808 runs ... COW BELL, CYMBAL, OPEN
   * HIHAT, CLOSED HIHAT, where the pads and the DSP end CH, OH, CY. It has to
   * be a permutation of the same set: an id that is missing silently drops an
   * instrument off the panel, and one that is misspelled draws nothing. */
  const panelSrc = html.slice(html.indexOf("var PANEL"), html.indexOf("function panelLanes"));
  const panel = [...panelSrc.matchAll(/"([a-z]{2})"/g)].map(m => m[1]);
  check(panel.length === order.length &&
        [...panel].sort().join() === [...order].sort().join(),
        "PANEL draws every lane exactly once" +
        (panel.length ? " (" + panel.join(" ") + ")" : ""));
}

/* ---- host mock ---- */
const params = { "synth:chain_params": CHAIN_PARAMS, "synth:ui_pages": UI_PAGES,
                 "synth:mutes": "0" };
for (const p of JSON.parse(CHAIN_PARAMS)) params["synth:" + p.key] = String(p.default ?? 0);
const setLog = [];
let shift = false, injected = [], announced = [];
Object.assign(globalThis, {
  shadow_get_ui_slot: () => 0,
  shadow_get_display_mode: () => 1,
  shadow_get_shift_held: () => shift,
  shadow_get_param: (slot, k) => (k in params ? params[k] : null),
  shadow_set_param: (slot, k, v) => { params[k] = String(v); setLog.push([k, String(v)]); },
  host_pad_block: () => {},
  host_announce_screenreader: t => announced.push(t),
  move_midi_inject_to_move: m => injected.push(m),
  clear_screen: () => {}, fill_rect: () => {},
  print: () => {}, text_width: s => s.length * 6,
  draw_line: () => {}, fill_circle: () => {}, draw_circle: () => {}, draw_arc: () => {},
});

/* ---- load ui_chain.js with its device imports pointed at the real library ----
 *
 * The static cross-checks above are worth running on their own, so a missing
 * library skips the live half rather than failing the file — but it says so
 * loudly, because silently skipping half a test suite is worse than not
 * having it.
 */
if (!SHARED) {
  console.log("\nSKIP: Schwung's shared param_pages library not found.");
  console.log("      Pass its path, set $SCHWUNG_SHARED, or check out schwung");
  console.log("      next to this repo. The static checks above still ran.");
  console.log(fails ? `\nFAILED (${fails})` : "\nSTATIC CHECKS PASS (live editor skipped)");
  process.exit(fails ? 1 : 0);
}
/* The title bar is a bitmap font on the device, so scraping the screen text
 * proves nothing. Wrap createController instead and record the title the
 * binding HANDS the renderer — 6W6's trick, and the honest reading. */
const spy = path.join(ROOT, "build-native", "pc_spy.mjs");
fs.mkdirSync(path.dirname(spy), { recursive: true });
fs.writeFileSync(spy, `
import { createController as real } from ${JSON.stringify(pathToFileURL(SHARED + "/param_pages/page_controller.mjs").href)};
export const spied = { title: null, ctl: null };
export function createController(...a) {
  const c = real(...a); spied.ctl = c;
  const r = c.render.bind(c);
  c.render = (ctx, opts) => { if (opts && opts.title != null) spied.title = opts.title; return r(ctx, opts); };
  return c;
}
`);
const src = fs.readFileSync(path.join(ROOT, "src/ui_chain.js"), "utf8")
  .replace(/\/data\/UserData\/schwung\/shared\/param_pages\/page_controller\.mjs/g,
           pathToFileURL(spy).href)
  .replace(/\/data\/UserData\/schwung\/shared\//g, pathToFileURL(SHARED + "/").href);
const tmp = path.join(ROOT, "build-native", "ui_chain.test.mjs");
fs.mkdirSync(path.dirname(tmp), { recursive: true });
fs.writeFileSync(tmp, src);
await import(pathToFileURL(tmp).href);
const { spied } = await import(pathToFileURL(spy).href);
const ui = globalThis.chain_ui;
check(ui && ui.init && ui.tick && ui.onMidiMessageInternal && ui.handleBack,
      "chain_ui exports the four hooks");

ui.init();
check(announced.includes("8W8"), "init announces 8W8");
ui.tick();
check(true, "first tick renders without throwing");

const note = (n, v) => ui.onMidiMessageInternal(new Uint8Array([0x90, n, v]));
const off  = n => ui.onMidiMessageInternal(new Uint8Array([0x80, n, 0]));

/* plain pad: reaches Move, no param writes beyond the focus publish */
injected = []; setLog.length = 0;
note(68, 100); off(68);
check(injected.length === 2 && injected[0][0] === 0x09 && injected[0][2] === 68,
      "plain pad press+release pass through to Move");
check(setLog.length === 1 && setLog[0][0] === "synth:ui_focus" && setLog[0][1] === "0",
      "plain pad publishes only synth:ui_focus=0 (BD lane)");
setLog.length = 0; note(68, 100); off(68); note(68, 100); off(68);
check(setLog.length === 0, "hitting the SAME pad again writes nothing");
setLog.length = 0; note(69, 100); off(69);
check(setLog.length === 1 && setLog[0][1] === "1", "moving to another pad does write (lane 1)");

/* the pads 6W6 never had: the third and fourth rows */
setLog.length = 0; note(85, 100); off(85);
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "9"),
      "pad 85 is the CLAVES (lane 9) — its own pad now, not a mode switch");
setLog.length = 0; note(92, 100); off(92);
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "12"),
      "pad 92 is the cowbell (lane 12)");

/* There is no master pad any more: sixteen drums fill the block, so pad 95 is
 * the CYMBAL and it plays like any other. Master is reached by the jog. */
injected = []; setLog.length = 0; note(95, 100); off(95);
check(injected.length === 2, "pad 95 is a drum now and reaches Move");
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "15"),
      "pad 95 publishes ui_focus=15 (the cymbal's lane)");

/* Shift+Pad: silent select -> mute_ms 60 then inject */
shift = true; injected = []; setLog.length = 0;
note(77, 100); off(77); shift = false;
check(setLog.some(([k, v]) => k === "synth:mute_ms" && v === "60"),
      "Shift+Pad arms the 60 ms silent-select window");
check(injected.length === 2, "Shift+Pad still reaches Move (white pad follows)");

/* Mute+Pad on a lane above bit 7 — the one 6W6's byte mask could not hold */
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 127]));   /* mute held */
setLog.length = 0; injected = [];
note(95, 100); off(95);                                      /* cymbal, lane 15 */
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 0]));
check(setLog.some(([k, v]) => k === "synth:mutes" && v === String(1 << 15)),
      "Mute+Pad toggles the cymbal lane (bit 15, needs the 16-bit mask)");
check(injected.length === 2, "Mute+Pad press still reaches Move");
ui.tick();
check(true, "tick with a muted lane renders");
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 127])); note(95, 100); off(95);
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 0]));
check(params["synth:mutes"] === "0", "second Mute+Pad clears it");

/* knob 1 on the current page (CC 71 delta) writes a synth param */
setLog.length = 0;
ui.onMidiMessageInternal(new Uint8Array([0xB0, 71, 1]));
ui.tick();
check(setLog.some(([k]) => k.startsWith("synth:") && !k.endsWith("mutes")),
      "knob turn writes a synth param (" + (setLog[0] ? setLog[0][0] : "none") + ")");

/* jog turns pages; jog click opens the picker; Back closes it */
ui.onMidiMessageInternal(new Uint8Array([0xB0, 14, 1])); ui.tick();
ui.onMidiMessageInternal(new Uint8Array([0xB0, 3, 127])); ui.tick();
check(ui.handleBack() === true, "Back with the picker open is consumed (closes it)");
check(ui.handleBack() === false, "Back with no picker exits the editor");

/* every page in the hierarchy must be reachable by the jog, and render */
{
  let threw = null;
  try { for (let i = 0; i < 40; i++) {
    ui.onMidiMessageInternal(new Uint8Array([0xB0, 14, 1])); ui.tick();
  } } catch (e) { threw = e; }
  check(!threw, "jogging through all 16 pages renders every one" + (threw ? " — " + threw : ""));
}

/* ---- Main-page jog lock ----------------------------------------------
 *
 * A jog click while already ON Main toggles a lock: pads keep playing and
 * keep recording, but the page stops chasing them, so the master knobs stay
 * under your hands while you jam. Shift+Pad still navigates — that gesture
 * is an explicit "take me there". The title shows [L].
 */
{
  const jogBack = () => ui.onMidiMessageInternal(new Uint8Array([0xB0, 14, 127]));
  const click   = () => ui.onMidiMessageInternal(new Uint8Array([0xB0, 3, 127]));
  /* SHIFT + jog click. A plain click belongs to the platform now: it
     activates a row on the host's trailing pages and opens the section list
     on Main, so the lock had to move off it. */
  const shiftClick = () => { shift = true; click(); shift = false; };
  const locked  = () => { ui.tick(); return /\[L\]/.test(spied.title || ""); };
  const controller_pickerOpen = () => !!(spied.ctl && spied.ctl.pickerOpen);
  /* walk back to Main — it is page 0, and 40 steps is more than the 17 pages */
  for (let i = 0; i < 40; i++) { jogBack(); }
  ui.tick();
  check(!locked(), "not locked to begin with (" + spied.title + ")");

  /* A PLAIN click must NOT touch the lock — that gesture is Schwung's, and
     on Main it opens the section list. Which means it leaves the picker OPEN,
     so close it again before the next gesture, exactly as a player would. */
  click();
  check(!locked(), "a plain jog click does not arm the lock — it is the host's");
  check(controller_pickerOpen(), "a plain jog click still opens the section list");
  ui.handleBack();

  shiftClick();
  check(locked(), "Shift+jog click on Main arms the lock (" + spied.title + ")");

  /* locked: the pad plays but the page does not move */
  setLog.length = 0; injected = [];
  note(70, 100); off(70);
  check(!setLog.some(([k]) => k === "synth:ui_focus"),
        "locked: a pad no longer moves the page");
  check(injected.length === 2,
        "locked: the pad still plays and still records");

  /* Shift+Pad is the explicit override */
  shift = true; setLog.length = 0;
  note(70, 100); off(70); shift = false;
  check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "2"),
        "locked: Shift+Pad still navigates (lane 2)");

  /* back to Main, then unlock — a click only toggles while ON Main */
  for (let i = 0; i < 40; i++) { jogBack(); }
  ui.tick();
  shiftClick();
  check(globalThis.__8w8_main_lock === false && !locked(),
        "a second Shift+jog click, on Main, unlocks (" + spied.title + ")");

  setLog.length = 0;
  note(71, 100); off(71);
  check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "3"),
        "unlocked: pads move the page again");

  /* the host re-evaluates this file every time the editor opens, so the
   * flag has to outlive module scope or the lock drops itself */
  globalThis.__8w8_main_lock = true;
  check(locked(),
        "the lock lives on globalThis, so re-entering the editor keeps it");
  globalThis.__8w8_main_lock = false;
}

/* unfocused: pads pass through, nothing else reacts */
globalThis.shadow_get_ui_slot = () => 1; setLog.length = 0; injected = [];
note(68, 100); ui.onMidiMessageInternal(new Uint8Array([0xB0, 71, 1]));
check(injected.length === 1 && setLog.length === 0,
      "unfocused slot: pads pass through, knobs ignored");

/* ---- every attack/decay pair must draw as ONE envelope across BOTH slots --
 *
 * param_pages spans an AD graphic across adjacent attack/decay knobs, and it
 * reads the row LEFT TO RIGHT: decay-then-attack is not a run it recognises,
 * so the graph collapses onto one slot and the neighbour draws a bare knob.
 * The maracas page shipped that way and it looked broken, which is what it
 * was. Ordering is a property of gen_params.py's dict, so nothing but a test
 * like this stops it drifting back.
 */
{
  const { buildMetaIndex } = await import(
    pathToFileURL(path.join(SHARED, "param_pages/param_meta.mjs")).href);
  const { resolveViz, VIZ_ENVELOPE } = await import(
    pathToFileURL(path.join(SHARED, "param_pages/viz.mjs")).href);

  const hierarchy = JSON.parse(UI_PAGES);
  const metaIndex = buildMetaIndex({ hierarchy, chainParams: JSON.parse(CHAIN_PARAMS) });

  let pairs = 0, opted = 0, bad = [];
  for (const [id, lvl] of Object.entries(hierarchy.levels || {})) {
    const keys = (lvl.knobs || []).slice(0, 8);
    const a = keys.findIndex(k => k && /_attack$/.test(k));
    const d = keys.findIndex(k => k && /_decay$/.test(k));
    if (a < 0 || d < 0) continue;

    /* Attack before Decay on every page, envelope or not — one convention. */
    if (!(a < d)) { bad.push(`${id}: decay@${d} sits before attack@${a}`); continue; }

    /*
     * viz:false is an OPT-OUT and must be honoured, not overridden. The bass
     * drum declares it because on the circuit engine "Attack" is a frequency
     * and Q jump and "Decay" is loop gain — neither is an envelope time, and
     * drawing an AD curve over them would be a lie the renderer tells. 9W9
     * learned that one the hard way. So: pages that opt out are exempt from
     * the span rule, pages that do not must actually get their two slots.
     */
    const meta = k => (metaIndex.get ? metaIndex.get(k) : metaIndex[k]) || {};
    /*
     * A page that opts out must ACTUALLY not draw an envelope. Skipping it
     * here on the strength of the declaration is what leaves a fake AD
     * graphic on the device: viz:false is load-bearing on the on-device
     * renderer, not a declaration for anyone else. The bass drum's Attack
     * is a click
     * LEVEL, so the graphic would be a lie about what the knob does.
     */
    if (meta(keys[a]).viz === false || meta(keys[d]).viz === false) {
      opted++;
      const { groups } = resolveViz({ keys, metaIndex });
      const leaked = groups.find(g => g.kind === VIZ_ENVELOPE &&
                                      g.keys.includes(keys[a]));
      check(!leaked, `${id}: viz:false really suppresses the envelope` +
            (leaked ? ` — one formed anyway over ${leaked.keys.join("+")}` : ""));
      continue;
    }

    pairs++;
    const { groups } = resolveViz({ keys, metaIndex });
    const env = groups.find(g => g.kind === VIZ_ENVELOPE && g.keys.includes(keys[a]));
    const span = env ? env.keys.filter(k => keys.includes(k)).length : 0;
    if (span < 2) bad.push(`${id}: attack@${a} decay@${d} span=${span}`);
  }
  check(pairs > 0, `${pairs} attack/decay pages draw an envelope, ${opted} opted out`);
  check(bad.length === 0,
        "every attack/decay pair spans two slots, attack first" +
        (bad.length ? " — " + bad.join("; ") : ""));
}

console.log(fails ? `\nFAILED (${fails})` : "\nALL PASS");
process.exit(fails ? 1 : 0);

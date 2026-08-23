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

const SHARED = process.argv[2] ||
  "/Users/gustavolima/Developer/schwung-overtake/src/shared";
const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
let fails = 0;
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
  const order = ["bd","sd","lt","mt","ht","lc","mc","hc","rs","ma","cp","cb","ch","oh","cy"];
  let bad = null;
  for (const p of dspPads) if (order[lane[p]] !== dsp[p]) bad = `pad ${p}`;
  check(!bad, "PAD2LANE indices match the DSP's lane order" + (bad ? " — " + bad : ""));
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
  clear_screen: () => {}, fill_rect: () => {}, print: () => {}, text_width: s => s.length * 6,
  draw_line: () => {}, fill_circle: () => {}, draw_circle: () => {}, draw_arc: () => {},
});

/* ---- load ui_chain.js with its device imports pointed at the real library ---- */
const src = fs.readFileSync(path.join(ROOT, "src/ui_chain.js"), "utf8")
  .replace(/\/data\/UserData\/schwung\/shared\//g, pathToFileURL(SHARED + "/").href);
const tmp = path.join(ROOT, "build-native", "ui_chain.test.mjs");
fs.mkdirSync(path.dirname(tmp), { recursive: true });
fs.writeFileSync(tmp, src);
await import(pathToFileURL(tmp).href);
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
setLog.length = 0; note(87, 100); off(87);
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "11"),
      "pad 87 is the COWBELL (lane 11) — it was Master in 6W6");
setLog.length = 0; note(94, 100); off(94);
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "14"),
      "pad 94 is the cymbal (lane 14)");

/* pad 16 = master page: never reaches Move */
injected = []; setLog.length = 0; note(95, 100); off(95);
check(injected.length === 0, "pad 95 (master) never sounds");
check(setLog.some(([k, v]) => k === "synth:ui_focus" && v === "15"),
      "pad 95 publishes ui_focus=15 (master)");

/* Shift+Pad: silent select -> mute_ms 60 then inject */
shift = true; injected = []; setLog.length = 0;
note(77, 100); off(77); shift = false;
check(setLog.some(([k, v]) => k === "synth:mute_ms" && v === "60"),
      "Shift+Pad arms the 60 ms silent-select window");
check(injected.length === 2, "Shift+Pad still reaches Move (white pad follows)");

/* Mute+Pad on a lane above bit 7 — the one 6W6's byte mask could not hold */
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 127]));   /* mute held */
setLog.length = 0; injected = [];
note(94, 100); off(94);                                      /* cymbal, lane 14 */
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 0]));
check(setLog.some(([k, v]) => k === "synth:mutes" && v === String(1 << 14)),
      "Mute+Pad toggles the cymbal lane (bit 14, needs the 15-bit mask)");
check(injected.length === 2, "Mute+Pad press still reaches Move");
ui.tick();
check(true, "tick with a muted lane renders");
ui.onMidiMessageInternal(new Uint8Array([0xB0, 88, 127])); note(94, 100); off(94);
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

/* unfocused: pads pass through, nothing else reacts */
globalThis.shadow_get_ui_slot = () => 1; setLog.length = 0; injected = [];
note(68, 100); ui.onMidiMessageInternal(new Uint8Array([0xB0, 71, 1]));
check(injected.length === 1 && setLog.length === 0,
      "unfocused slot: pads pass through, knobs ignored");

console.log(fails ? `\nFAILED (${fails})` : "\nALL PASS");
process.exit(fails ? 1 : 0);

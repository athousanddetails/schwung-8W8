import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("../src/ui_chain.js", import.meta.url), "utf8")
    .replace(/^import .*;\n/gm, "")
    .replace(/^const PAGE_.*;\n/gm, "");

let tts = true;
let shift = false;
const spoken = [];
const layouts = [];
const writes = [];
const page = { level: "root", kind: "knobs" };

const controller = {
    pages: [page, { level: "bd", kind: "knobs" }],
    page,
    pickerOpen: false,
    knobRows() { return []; },
    load() { this.io.announce("8W8, 1 of 19, 7 controls"); },
    setLayout(layout) { layouts.push(layout); },
    showHint() { throw new Error("visual hint must not cover the accessible list"); },
    setReveal() {}, tick() {}, render() {}, goToPage() {},
    dismissHint() { return false; }, dismissPeek() { return false; },
    exitMenu() { return false; }, closePicker() {},
};

const context = vm.createContext({
    console,
    Date,
    PagePlan: { PAGE_KNOBS: "knobs", PAGE_MENU: "menu" },
    PAGE_KNOBS: "knobs",
    PAGE_MENU: "menu",
    LAYOUT_MOVY: "movy",
    createController(io) { controller.io = io; return controller; },
    decodeInput(data, mods) {
        if ((data[0] & 0xF0) === 0xB0 && data[1] === 3 && data[2] > 0)
            return { type: "click", shift: !!mods.shift };
        return null;
    },
    applyInput() { return null; },
    tts_get_enabled() { return tts ? 1 : 0; },
    host_announce_screenreader(text) { spoken.push(String(text)); },
    shadow_get_ui_slot() { return 0; },
    shadow_get_display_mode() { return 1; },
    shadow_get_shift_held() { return shift ? 1 : 0; },
    shadow_get_param(slot, key) { return key === "synth:mutes" ? "0" : ""; },
    shadow_set_param(slot, key, value) { writes.push([slot, key, value]); },
    host_pad_block() {},
    move_midi_inject_to_move() {},
    clear_screen() {}, fill_rect() {}, print() {}, text_width() { return 0; },
});

vm.runInContext(source, context, { filename: "ui_chain.js" });
context.chain_ui.init();

assert.equal(layouts[0], "list", "TTS must select the accessible list layout");
assert.deepEqual(spoken.slice(0, 2), ["8W8", "8W8, 1 of 19, 7 controls"],
    "the actionable page announcement must follow the module name");

context.chain_ui.onMidiMessageInternal([0xB0, 88, 127]);
const lanePads = [68, 69, 70, 71, 76, 77, 78, 79, 84, 85, 86, 87, 92, 93, 94, 95];
const laneNames = ["Bass Drum", "Snare", "Low Tom", "Mid Tom", "Hi Tom",
    "Low Conga", "Mid Conga", "Hi Conga", "Rim Shot", "Claves", "Maracas",
    "Hand Clap", "Cowbell", "Closed Hat", "Open Hat", "Cymbal"];
for (let lane = 0; lane < lanePads.length; lane++) {
    context.chain_ui.onMidiMessageInternal([0x90, lanePads[lane], 100]);
    assert.equal(spoken.at(-1), `${laneNames[lane]} muted`,
        `physical pad ${lanePads[lane]} must name DSP lane ${lane}`);
}
assert.ok(writes.some(([, key, value]) => key === "synth:mutes" && value === "65535"),
    "all sixteen mute bits must survive the 16-bit mask");

context.chain_ui.onMidiMessageInternal([0xB0, 88, 0]);
shift = true;
context.chain_ui.onMidiMessageInternal([0xB0, 3, 127]);
assert.equal(spoken.at(-1), "Main page locked");
context.chain_ui.onMidiMessageInternal([0xB0, 3, 127]);
assert.equal(spoken.at(-1), "Main page unlocked");

controller.knobRows = undefined;
tts = true;
shift = false;
context.chain_ui.tick();
assert.equal(layouts.at(-1), "movy",
    "an older controller without list support must keep the working knob grid");

tts = false;
context.chain_ui.tick();
assert.equal(layouts.at(-1), "movy", "disabling TTS must restore the knob grid");

console.log("PASS: accessible list, announcement order, 16-lane mutes, and Main lock");

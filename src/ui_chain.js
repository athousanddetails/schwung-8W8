/*
 * 8W8 ui_chain.js — a thin binding over Schwung's shared param_pages library
 * (host 0.12.1+), keeping every gesture the stock hierarchy editor does not
 * have. Ported from 6W6 with an 808 roster: the three kits must feel
 * identical under the hands, so the gestures, the hint and the focus gate are
 * the same code with different tables.
 *
 * 8W8 fills the pad block where 6W6 half-filled it — SIXTEEN drums, one per
 * sc808 SynthDef — so the tables below are the only real difference, plus a
 * mute mask that needs sixteen bits rather than eight. There is no Master pad
 * to handle, because there is no pad left for it.
 *
 * Division of labour:
 *   param_pages (stock)          this file (8W8)
 *   ------------------          ---------------------------------
 *   knob grid, dial layout      pads pass through to Move + page-follow
 *   viz graphics (faders...)    Shift+Pad silent select (white pad follows)
 *   jog page / Shift+Jog        Mute+Pad per-lane 8W8 mutes
 *   section picker (jog click)  focus gate (never steal another slot's pads)
 *   hold-knob name strip        pad_block lifecycle + self-heal
 *   Shift reveal + fine mode
 *   Mute+knob reset-to-default
 *
 * Why this file still exists at all: the host's enterComponentEdit prefers a
 * module's ui_hierarchy and would never load ui_chain.js if we served one — so
 * the DSP serves the hierarchy under "ui_pages" instead, and the injected
 * getParam below rewrites the controller's "ui_hierarchy" read to it. Pads are
 * not part of the stock grid's input model, so pad behaviour stays ours.
 *
 * GPL-3.0. param_pages © Schwung contributors.
 */

import { createController } from '/data/UserData/schwung/shared/param_pages/page_controller.mjs';
import { decodeInput, applyInput } from '/data/UserData/schwung/shared/param_pages/page_input.mjs';
import { PAGE_KNOBS, PAGE_MENU } from '/data/UserData/schwung/shared/param_pages/page_plan.mjs';
import { LAYOUT_MOVY } from '/data/UserData/schwung/shared/param_pages/render_page_movy.mjs';

(function () {
    "use strict";

    /* raw pad note -> page key (page-follow). The left 4x4 block, bottom row
     * first: BD SD LT MT / HT LC MC HC / RS CL MA CP / CB CH OH CY. These
     * must match pad_to_voice() in sc808_plugin.cpp — the DSP decides what
     * sounds, this decides what the screen shows, and them disagreeing is a
     * bug nobody would spot quickly. */
    var PAD2LEVEL = { 68: "bd", 69: "sd", 70: "lt", 71: "mt",
                      76: "ht", 77: "lc", 78: "mc", 79: "hc",
                      84: "rs", 85: "cl", 86: "ma", 87: "cp",
                      92: "cb", 93: "ch", 94: "oh", 95: "cy" };

    /* raw pad note -> 8W8 lane (Mute+Pad). Same order as the DSP's enum. */
    var PAD2LANE = { 68: 0, 69: 1, 70: 2, 71: 3, 76: 4, 77: 5, 78: 6, 79: 7,
                     84: 8, 85: 9, 86: 10, 87: 11, 92: 12, 93: 13, 94: 14,
                     95: 15 };

    /* page key -> lane whose mute the title indicator shows (-1 = none) */
    var LEVEL2LANE = { bd: 0, sd: 1, lt: 2, mt: 3, ht: 4, lc: 5, mc: 6, hc: 7,
                       rs: 8, cl: 9, ma: 10, cp: 11, cb: 12, ch: 13, oh: 14,
                       cy: 15, root: -1,
                       /* the two send buses have no lane and so no mute flag */
                       rev: -1, dly: -1 };

    /*
     * MAIN-PAGE LOCK. A jog click while already ON the Main page toggles it.
     * Locked, the pads still play and still record but no longer switch the
     * page — so the master knobs stay under your hands while you jam the
     * kit. Shift+Pad still selects: that gesture IS an explicit "take me
     * there". The title shows [L].
     *
     * It lives on globalThis deliberately. The host re-evaluates this file
     * every time the editor is opened, so module-level state would reset and
     * the lock would look like it had dropped itself.
     */
    function mainLocked() { return !!globalThis.__8w8_main_lock; }

    function onMainPage() {
        var page = controller && controller.page;
        return !!page && (page.level === "root" || page.level == null);
    }

    /* The lane index that means "Master" to the DSP's ui_focus. Sixteen
     * drums, so Master is 16 — and there is NO master pad any more: 9W9 and
     * 6W6 put it on pad 16, and with sixteen drums the block is full. Master
     * is reached by the jog, like every other page. */
    var MASTER_LANE = 16;

    /* Sixteen lanes: the mute mask needs sixteen bits. 6W6 used a byte, and
     * masking with 0xFF here would silently drop the whole top row. */
    var LANE_MASK = 0xFFFF;

    var mySlot = -1;
    var padBlocked = false;
    var muteHeld = false;
    var mutesMask = 0;
    var controller = null;

    /* First-run gesture hint: once per shadow_ui session, dismissed by any
     * input -- including pads, which are our layer -- or on its own after
     * HINT_MS. A hint nobody can wave away feels stuck. The "shown" flag lives
     * on globalThis: the host re-evaluates this file on every editor open, so
     * module-level state would reset each time; the shadow_ui process's global
     * object is what actually lives for the session. */
    var HINT_MS = 4000;
    var HINT_FLAG = "__8w8_hint_shown";
    var hintUntil = 0;

    function dismissHint() {
        hintUntil = 0;
        if (controller && controller.dismissHint) controller.dismissHint();
    }

    function has(fn) { return typeof globalThis[fn] === "function"; }

    function uiSlot() {
        return has("shadow_get_ui_slot") ? shadow_get_ui_slot() : 0;
    }

    function isFocused() {
        return mySlot >= 0 && uiSlot() === mySlot;
    }

    function setPadBlock(on) {
        if (padBlocked === on) return;
        if (has("host_pad_block")) { host_pad_block(on ? 1 : 0); padBlocked = on; }
    }

    function shiftHeld() {
        return has("shadow_get_shift_held") && !!shadow_get_shift_held();
    }

    /* The controller's device I/O. One special case: its "ui_hierarchy" read
     * is rewritten to "ui_pages" — the key the DSP actually serves, because
     * serving ui_hierarchy itself would stop this file from ever loading. */
    function ctlGetParam(key) {
        if (!has("shadow_get_param")) return null;
        if (key === "synth:ui_hierarchy") key = "synth:ui_pages";
        return shadow_get_param(mySlot, key);
    }

    function ctlSetParam(key, value) {
        if (has("shadow_set_param")) shadow_set_param(mySlot, key, String(value));
    }

    function announce(text) {
        if (has("host_announce_screenreader")) host_announce_screenreader(text);
    }

    function refreshMutes() {
        var m = parseInt(ctlGetParam("synth:mutes"), 10);
        mutesMask = isNaN(m) ? 0 : m;
    }

    function toggleLaneMute(lane) {
        mutesMask = (mutesMask ^ (1 << lane)) & LANE_MASK;
        ctlSetParam("synth:mutes", String(mutesMask));
    }

    /* Jump the grid to the first page of a hierarchy level, and publish the
     * page id so the remote panel follows. A set_param, not a read: the shim
     * only tells schwung-manager about WRITES. */
    var lastFocus = -1;
    function goToLevel(levelKey) {
        if (!controller) return;
        var lane = LEVEL2LANE[levelKey];
        if (lane === undefined || lane < 0) lane = MASTER_LANE;
        /* Only on a CHANGE. Every pad press calls this, and a param write is a
         * ~2.8 ms round-trip on the shared channel — more than a whole screen
         * redraw costs — so re-announcing the page you are already on is pure
         * waste on the path the editor needs for its own reads. */
        if (lane !== lastFocus) {
            lastFocus = lane;
            ctlSetParam("synth:ui_focus", String(lane));
        }
        var pages = controller.pages;
        for (var i = 0; i < pages.length; i++) {
            if (pages[i].level === levelKey ||
                (levelKey === "root" && pages[i].level === null)) {
                controller.goToPage(i);
                return;
            }
        }
        if (levelKey === "root") controller.goToPage(0);
    }

    /* Give the pad back to Move as a real press: plays the (HiJack-muted) kit,
     * records while REC is on, and updates Move's pad selection. */
    function injectToMove(data) {
        if (!has("move_midi_inject_to_move")) return;
        var type = (data[0] & 0xF0) === 0x90 ? 0x09
                 : (data[0] & 0xF0) === 0x80 ? 0x08
                 : (data[0] & 0xF0) === 0xA0 ? 0x0A : 0;
        if (!type) return;
        move_midi_inject_to_move([type, data[0], data[1], data[2]]);
    }

    /* ---------------- chain_ui hooks ---------------- */

    function init() {
        mySlot = uiSlot();
        setPadBlock(true);
        refreshMutes();

        controller = createController({
            getParam: ctlGetParam,
            setParam: ctlSetParam,
            announce: announce,
            /*
             * The host's own trailing pages — My Presets and Module. Every
             * sound generator on the STOCK hierarchy editor gets them; a
             * module that ships its own ui_chain.js built its own controller
             * and got neither, because the host only handed them to its own.
             * These menus are the host's to build: the preset record lives in
             * the slot config, not in us.
             *
             * Guarded, because the binding is absent on an older host and
             * absent for a Master FX position, which has no preset record.
             * Without it the array is empty and nothing is appended — exactly
             * how this behaved before.
             */
            trailingMenus: function () {
                return (typeof shadow_component_trailing_menus === "function")
                    ? (shadow_component_trailing_menus() || [])
                    : [];
            }
        });
        controller.load({ slot: mySlot, component: "synth", prefix: "synth" });
        controller.setLayout(LAYOUT_MOVY);
        if (!globalThis[HINT_FLAG]) {
            globalThis[HINT_FLAG] = true;
            controller.showHint([
                "Pad: play + select",
                "Sh+Pad: select only",
                "Mute+Pad: mute drum",
                "Jog: page  Click: list",
                "Sh+Click Main: lock",
                "Shift: fine + values",
                "Mute+knob: default"
            ], "8W8");
            hintUntil = Date.now() + HINT_MS;
        }
        announce("8W8");
    }

    /* Title-bar text. The stock grid prints the page's own name on the right
     * of the bar, so this must NOT repeat it ("8W8 > BASS DRUM  BASS DRUM"):
     * just the module name plus the mute flag for the drum on screen. */
    function title() {
        var t = "8W8";
        if (mainLocked()) t += " [L]";
        var page = controller && controller.page;
        var lane = page ? LEVEL2LANE[page.level] : -1;
        if (lane !== undefined && lane >= 0 && (mutesMask & (1 << lane)))
            t += " [M]";
        return t;
    }

    function tick() {
        var shown = !has("shadow_get_display_mode") || shadow_get_display_mode() === 1;
        var active = shown && isFocused();
        setPadBlock(active);
        if (!active || !controller) return;

        if (hintUntil && Date.now() >= hintUntil) dismissHint();
        controller.setReveal(shiftHeld());
        controller.tick();

        /* The grid paces its own redraws; draw every tick like the stock
         * binding does (a full page render is ~1.6 ms, measured upstream). */
        clear_screen();
        var page = controller.page;
        /*
         * PAGE_MENU as well as the grid. A menu page is a list of actions
         * with no params behind it, and the LIBRARY draws it. Excluding it
         * here would print the unsupported-page fallback over a page
         * param_pages was about to draw correctly — which is what the host's
         * My Presets and Module pages arrive as.
         */
        if (controller.pickerOpen ||
            (page && (page.kind === PAGE_KNOBS || page.kind === PAGE_MENU))) {
            controller.render(
                {
                    fillRect: fill_rect, print: print, textWidth: text_width,
                    line: typeof draw_line === "function" ? draw_line : undefined,
                    fillCircle: typeof fill_circle === "function" ? fill_circle : undefined,
                    drawCircle: typeof draw_circle === "function" ? draw_circle : undefined,
                    drawArc: typeof draw_arc === "function" ? draw_arc : undefined
                },
                { title: title() }
            );
            /*
             * THE SECOND HALF OF THE DRAW, and it is not optional.
             *
             * render() paints a page into a rect the CALLER owns; nothing in
             * param_pages clears the screen, which is what lets a consumer
             * host a page inside its own chrome. So anything FULL-SCREEN is
             * handed back to the frame owner -- and that is us.
             *
             * Today that means the enum peek: turn a multi-option enum and its
             * option list rises over the grid for ~700ms. Without this call
             * the controller still tracks the peek and applyInput still
             * swallows the Back that dismisses it; it is simply painted
             * nowhere. Found on CW-78 by Charles (charlesvestal, schwung-cw-78
             * PR #1), then in 9W9, then here -- the same one-line omission,
             * copied per module, drifting silently. His fix, verbatim.
             *
             * Guarded because renderOverlays landed in a later host than this
             * file's min_host_version, and an older host simply has no
             * overlays to draw.
             */
            if (typeof controller.renderOverlays === "function") {
                controller.renderOverlays(
                    { fillRect: fill_rect, print: print, textWidth: text_width },
                    { clearScreen: clear_screen }
                );
            }
        } else {
            /* Non-grid page kinds do not occur in 8W8's hierarchy; if one ever
             * does, show something honest instead of a stale frame. */
            print(2, 28, "8W8: unsupported page", 1);
        }
    }

    function onMidiMessageInternal(data) {
        var status = data[0] & 0xF0;
        var d1 = data[1];
        var d2 = data[2];

        /* Another slot is focused: never react; keep the surface alive. */
        if (!isFocused()) {
            if ((status === 0x90 || status === 0x80 || status === 0xA0) &&
                d1 >= 68 && d1 <= 99)
                injectToMove(data);
            return;
        }

        /* Mute button held-state (CC 88): ours for Mute+Pad, the library's
         * for Mute+knob reset — tracked here, passed to decodeInput below. */
        if (status === 0xB0 && d1 === 88) {
            muteHeld = (d2 > 0);
            return;
        }

        /* ---- Pads: 8W8's own layer ---- */
        if ((status === 0x90 || status === 0x80 || status === 0xA0) &&
            d1 >= 68 && d1 <= 99) {

            if (status === 0x90 && d2 > 0) dismissHint();

            /* Mute + Pad: toggle that lane's 8W8 mute; press still reaches
             * Move so its native state stays in step. */
            if (muteHeld) {
                var lane = PAD2LANE[d1];
                if (status === 0x90 && d2 > 0 && lane !== undefined)
                    toggleLaneMute(lane);
                injectToMove(data);
                return;
            }

            var level = PAD2LEVEL[d1];

            /* Locked to Main: the pad plays, the page stays. Every 8W8 pad
             * is a drum — there are no page-only pads to withhold, because
             * sixteen voices fill the block — so this is unconditional. */
            if (mainLocked() && !shiftHeld()) {
                injectToMove(data);
                return;
            }

            if (shiftHeld()) {
                /* Silent select: page follows AND Move's white pad follows —
                 * the DSP swallows exactly the one note routed back (60 ms
                 * window). Accepted trade: with REC armed and playing, this
                 * press would be recorded; Gus does not use REC. */
                if (status === 0x90 && d2 > 0) {
                    if (level !== undefined) goToLevel(level);
                    ctlSetParam("synth:mute_ms", "60");
                    injectToMove(data);
                } else {
                    injectToMove(data);        /* matching release */
                }
                return;
            }

            /* Plain pad: page follows what you play; Move plays/records.
             * Every pad in the block is a drum now, so every one of them
             * sounds and every one of them reaches Move. */
            if (status === 0x90 && d2 > 0 && level !== undefined)
                goToLevel(level);
            injectToMove(data);
            return;
        }

        /* ---- Everything else: the stock grid's input model ---- */
        if (!controller) return;
        var intent = decodeInput(data, { shift: shiftHeld(), mute: muteHeld });
        if (!intent) return;
        /*
         * SHIFT + jog click, not a plain click — and the difference is not
         * cosmetic. A plain click now belongs to the platform: on the host's
         * trailing pages it activates a row, and on Main it opens the section
         * list. Anything of ours on that gesture would collide with both, so
         * the lock moved. Before applyInput either way, or the picker
         * consumes it.
         */
        if (intent.type === "click" && shiftHeld() &&
            !controller.pickerOpen && onMainPage()) {
            globalThis.__8w8_main_lock = !globalThis.__8w8_main_lock;
            return;                   /* Shift+click = lock toggle */
        }
        var todo = applyInput(controller, intent, { nowMs: Date.now(), reveal: false });
        /*
         * A menu row was activated. The controller reports it as
         * { action: "menu", entry } — the ENTRY carries the action key
         * (up_save_as, swap_module ...); "menu" is only the intent's KIND.
         * Handing the host the word "menu" runs nothing, silently, which is
         * the mistake 9W9 made first. Performed by the Shadow UI, not here:
         * those keys reach the preset store, the browser, the component
         * picker and the help screen, none of which a module can address.
         */
        if (todo && todo.action === "menu") {
            var act = todo.entry && todo.entry.action;
            if (act && typeof shadow_component_run_action === "function")
                shadow_component_run_action(act);
            return;
        }
        if (todo && todo.action === "exit") {
            /* Back never reaches us (the host consumes it); any other exit
             * intent just closes the picker. */
            if (controller.pickerOpen) controller.closePicker();
        }
        /* 'open' (opaque param editors) cannot occur: every 8W8 param is an
         * int or an enum. Ignored if a future param ever produces one. */
    }

    function onMidiMessageExternal(data) { }

    /*
     * The host consumes Back and calls this FIRST, so it has to climb the
     * same rungs page_input.mjs's own `case "back"` does, in its order —
     * hint, peek, picker, menu — or Back means something different here than
     * on every stock grid. Without the menu rung, Back from inside My Presets
     * leaves the module entirely and skips the page bar.
     */
    function handleBack() {
        if (!controller) { setPadBlock(false); return false; }
        if (controller.dismissHint && controller.dismissHint()) return true;
        if (controller.dismissPeek && controller.dismissPeek()) return true;
        if (controller.pickerOpen) { controller.closePicker(); return true; }
        /* out of the MENU, not out of the module */
        if (controller.exitMenu && controller.exitMenu()) return true;
        setPadBlock(false);
        return false;                          /* host exits the editor */
    }

    globalThis.chain_ui = {
        init: init,
        tick: tick,
        onMidiMessageInternal: onMidiMessageInternal,
        onMidiMessageExternal: onMidiMessageExternal,
        handleBack: handleBack,
        /* A preset was saved or loaded while our grid is on screen. The My
         * Presets row is built by OUR controller from the host's menus, so
         * nothing else would refresh it and it would go on reading "(none)"
         * after a Save. */
        onPresetsChanged: function () {
            if (controller && typeof controller.refreshTrailing === "function")
                controller.refreshTrailing();
        },
        /* After Load, Delete, Swap or Help the host reloads us and says which
         * page we left from and whether to land inside its menu. The
         * controller keeps the request armed until its pages arrive, so a
         * contract still settling after the reload is fine. Without this,
         * every return landed on Main. */
        restorePage: function (name, opts) {
            if (controller && typeof controller.restorePage === "function")
                controller.restorePage(name, opts || {});
        }
    };
})();

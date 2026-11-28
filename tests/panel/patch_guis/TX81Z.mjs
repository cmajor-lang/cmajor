// Tests for the TX81Z patch's GUI (cmajor/examples/patches/TX81Z/gui), run by test-patch-guis.mjs
// after the checks that every patch GUI gets. See README.md for how these work.
//
// The GUI starts with the patch's initial parameter values, so as the voice "LatelyBass" (the
// GUI's default name), algorithm 2, volume -6 dB, and so on: if those change, so must these.
//
// The worker runs too, so the program buttons and the voice list load voices from the bank.

import { join } from "node:path";
import { sleep } from "../harness/patch-gui.mjs";

const screenshots = process.env.PANEL_TEST_SCREENSHOTS;
const size = { width: 1430, height: 768 };

export default async function testTX81Z (guis, name, t)
{
    await testControls (guis, name, t);
    await testNameEditing (guis, name, t);

    // (the pointer handling has to work whether or not pointer capture holds, which it
    // doesn't in some hosts' webviews)
    for (const mode of ["capture", "nocapture"])
    {
        await testProgramButtons (guis, name, t, mode);
        await testReleasingOutsideControls (guis, name, t, mode);
    }
}

//==============================================================================
/// The messages the view has sent, as [kind, endpoint or key, value]: kind is "value", "midi",
/// "gestureStart", "gestureEnd" or "state"
async function getSent (page)
{
    return (await page.getSent()).map (m =>
    {
        switch (m.type)
        {
            case "send_value":          return typeof m.value?.message === "number" ? ["midi", m.id, m.value.message] : ["value", m.id, m.value];
            case "send_gesture_start":  return ["gestureStart", m.id];
            case "send_gesture_end":    return ["gestureEnd", m.id];
            case "send_state_value":    return ["state", m.key, m.value];
            default:                    return [m.type];
        }
    });
}

const getLCDText = (page) => page.evaluate ("window.view.lcd.lines.map (l => l.replace (/[\\x00-\\x07]/g, '#'))");
const getParameterValue = (page, id) => page.evaluate (`window.view.controls.get (${JSON.stringify (id)})?.param.value`);
const getPatchParameterValue = (page, id) => page.evaluate (`window.session.getParameterValue (window.session.findEndpoint (${JSON.stringify (id)}))`);

async function screenshot (page, file, selector)
{
    if (screenshots)
    {
        const r = selector ? await page.rectOf (selector) : null;
        await page.screenshot (join (screenshots, "TX81Z-" + file), r ? { x: r.left, y: r.top, width: r.width, height: r.height } : undefined);
    }
}

async function doubleClick (page, x, y)
{
    await page.mouse ("mousePressed", x, y, { buttons: 1, clickCount: 1 });
    await page.mouse ("mouseReleased", x, y, { clickCount: 1 });
    await page.mouse ("mousePressed", x, y, { buttons: 1, clickCount: 2 });
    await page.mouse ("mouseReleased", x, y, { clickCount: 2 });
}

async function checkNoErrors (page, t, what)
{
    const errors = await page.getErrors();
    t.check (errors.length === 0, `no errors ${what}: ${errors.join ("; ")}`);
}

//==============================================================================
async function testControls (guis, name, t)
{
    const page = await guis.open (name, size);
    await screenshot (page, "01-idle.png");

    // --- the initial state
    let lcd = await getLCDText (page);
    t.check (lcd[0] === "I01  LatelyBass ", "the idle LCD shows the voice name: " + JSON.stringify (lcd));
    t.check (lcd[1].includes ("ALG 2") && lcd[1].includes ("VOL-6dB"), "the idle LCD shows algorithm 2 and the volume: " + JSON.stringify (lcd));
    t.check (await page.inView ("this.shadowRoot.querySelectorAll ('.algorithm.selected').length === 1"), "one algorithm is selected");
    t.check (await getParameterValue (page, "osc4FeedbackIn") === 7, "the initial values arrived (osc4 feedback is 7)");

    // --- dragging a knob
    await page.clearSent();
    let r = await page.rectOf (`[data-parameter="osc1FreqMultiplierIn"] .knob-area`);
    await page.drag (r.x, r.y, r.x, r.y - 50, 10);
    await sleep (50);
    let sent = await getSent (page);
    const ratioValues = sent.filter (s => s[0] === "value" && s[1] === "osc1FreqMultiplierIn").map (s => s[2]);
    t.check (sent[0]?.[0] === "gestureStart" && sent.at (-1)?.[0] === "gestureEnd", "a knob drag is wrapped in a gesture");
    t.check (ratioValues.length > 3 && Math.abs (ratioValues.at (-1) - (0.5 + 16 * 50 / 200)) < 0.3, "the ratio knob moves by a quarter turn: " + ratioValues.at (-1));
    lcd = await getLCDText (page);
    t.check (lcd[0].startsWith ("OP1 Freq Ratio"), "the LCD shows the knob's parameter: " + JSON.stringify (lcd));
    await screenshot (page, "02-lcd-ratio.png", "#lcd-window");

    // --- a stepped knob
    await page.clearSent();
    r = await page.rectOf (`[data-parameter="osc3WaveshapeIn"] .knob-area`);
    await page.drag (r.x, r.y, r.x, r.y - 60, 6);
    sent = await getSent (page);
    const waveValues = sent.filter (s => s[0] === "value").map (s => s[2]);
    t.check (waveValues.every (v => Number.isInteger (v)), "the wave knob only sends integers: " + waveValues.join (","));
    lcd = await getLCDText (page);
    t.check (/^OP3 Waveform/.test (lcd[0]) && lcd[1].endsWith ("########"), "the LCD shows the waveform's picture: " + JSON.stringify (lcd));
    await screenshot (page, "03-lcd-wave.png", "#lcd-window");

    // --- a slider: click on its slot, then drag
    await page.clearSent();
    r = await page.rectOf (`[data-parameter="osc2AttackRateIn"] .slider-area`);
    await page.drag (r.x, r.top + r.height * 0.75, r.x, r.top + r.height * 0.5, 8);
    sent = await getSent (page);
    const attackValues = sent.filter (s => s[0] === "value").map (s => s[2]);
    t.check (attackValues.length > 0 && Math.abs (attackValues.at (-1) - 16) <= 2, "the attack-rate slider follows the mouse to half-way: " + attackValues.join (","));
    lcd = await getLCDText (page);
    t.check (lcd[0].startsWith ("OP2 Attack Rate"), "the LCD shows the slider's parameter: " + JSON.stringify (lcd));
    await screenshot (page, "04-lcd-bar.png", "#lcd-window");

    // --- double-clicking resets
    r = await page.rectOf (`[data-parameter="osc2AttackRateIn"] .slider-area`);
    await doubleClick (page, r.x, r.y);
    await sleep (50);
    t.check (await getParameterValue (page, "osc2AttackRateIn") === 31, "double-clicking resets the attack rate to 31");

    // --- the algorithm chart
    await page.clearSent();
    r = await page.rectOf (".algorithm:nth-of-type(5)");
    await page.click (r.x, r.y);
    await sleep (50);
    sent = await getSent (page);
    t.check (sent.some (s => s[1] === "algorithmIn" && s[2] === 5), "clicking the chart selects algorithm 5");
    const tags = await page.inView ("this.carrierTags.map (t => t.textContent)");
    t.check (JSON.stringify (tags) === JSON.stringify (["CARRIER", "MODULATOR", "CARRIER", "MODULATOR"]), "the carrier tags follow the algorithm: " + tags);

    // --- a change from the host shows on the LCD
    await page.evaluate (`window.hostSetsValue ("volumeIn", -20); 1`);
    await sleep (50);
    lcd = await getLCDText (page);
    t.check (lcd[0].startsWith ("Master Volume") && lcd[1].endsWith ("-20dB"), "a change from the host shows on the LCD: " + JSON.stringify (lcd));

    // --- the keyboard: press a key, and glide to the next white one
    await page.clearSent();
    r = await page.rectOf (".keyboard");
    const keyY = r.top + r.height * 0.9;
    await page.mouse ("mouseMoved", r.left + 5, keyY);
    await page.mouse ("mousePressed", r.left + 5, keyY, { buttons: 1 });
    await page.mouse ("mouseMoved", r.left + 5 + r.width / 36, keyY, { buttons: 1 });
    await page.mouse ("mouseReleased", r.left + 5 + r.width / 36, keyY);
    await sleep (50);
    const midi = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2].toString (16));
    t.check (midi.length === 4 && midi[0].startsWith ("9024") && midi[1] === "802400" && midi[2].startsWith ("9026") && midi[3] === "802600",
             "gliding along the keyboard plays C2 then D2: " + midi.join (" "));
    t.check (await page.inView ("this.shadowRoot.querySelectorAll ('.key.down').length === 0"), "the keys are released");

    // --- MIDI from elsewhere lights the keys
    await page.evaluate (`window.receiveEvent ("midiIn", { message: 0x903c64 }); 1`);
    await sleep (50);
    t.check (await page.inView ("this.keyboard.keys.get (60).classList.contains ('down')"), "an incoming note-on lights its key");
    await page.evaluate (`window.receiveEvent ("midiIn", { message: 0x803c00 }); 1`);
    await sleep (50);
    t.check (await page.inView ("! this.keyboard.keys.get (60).classList.contains ('down')"), "an incoming note-off releases it");

    // --- the pitch wheel
    await page.clearSent();
    r = await page.rectOf (".wheel-housing");
    await page.mouse ("mouseMoved", r.x, r.y);
    await page.mouse ("mousePressed", r.x, r.y, { buttons: 1 });

    for (let i = 1; i <= 10; ++i)
        await page.mouse ("mouseMoved", r.x, r.y - i * 10, { buttons: 1 });

    await screenshot (page, "05-pitch-bent.png");
    await page.mouse ("mouseReleased", r.x, r.y - 100);
    await sleep (600);
    const bendMessages = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2]);
    const bends = bendMessages.map (m => (((m & 0x7f) << 7) | ((m >> 8) & 0x7f)) - 8192);
    const maxBend = Math.max (...bends);
    t.check ((bendMessages[0] >> 16) === 0xe0, "the pitch wheel sends pitch bend");
    t.check (maxBend > 200 && maxBend <= 342, "the bend is limited to +/-2 semitones of the 48-semitone range: " + maxBend);
    t.check (bends.at (-1) === 0, "the pitch wheel springs back to the centre: " + bends.join (","));

    // --- the mod wheel
    await page.clearSent();
    r = await page.evaluate (`(() => { const e = window.view.shadowRoot.querySelectorAll (".wheel-housing")[1].getBoundingClientRect(); return { x: e.left + e.width / 2, y: e.top + e.height / 2 }; })()`);
    await page.drag (r.x, r.y, r.x, r.y - 60, 6);
    await sleep (300);
    const modMessages = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2]);
    t.check (modMessages.length > 0 && modMessages.every (m => (m >> 8) === 0xb001), "the mod wheel sends CC1");

    // --- renaming the voice
    await page.clearSent();
    r = await page.rectOf ("#lcd-window");
    await doubleClick (page, r.x, r.y);

    for (const ch of "Bass 1")
        await page.key (ch, ch === " " ? "Space" : "Key" + ch.toUpperCase());

    for (let i = 0; i < 12; ++i)
        await page.key ("Delete", "Delete");

    await page.key ("Enter", "Enter");
    await sleep (50);
    sent = await getSent (page);
    t.check (sent.some (s => s[0] === "state" && s[1] === "voiceName" && s[2] === "Bass 1"),
             "the voice name is saved in the stored state: " + JSON.stringify (sent.filter (s => s[0] === "state")));
    t.check (await page.evaluate (`window.getStoredState ("voiceName")`) === "Bass 1", "the patch has the new name");
    await sleep (2000);
    lcd = await getLCDText (page);
    t.check (lcd[0] === "I01  Bass 1     ", "the idle LCD shows the new name: " + JSON.stringify (lcd));

    // --- the computer keyboard
    await page.clearSent();
    await page.send ("Input.dispatchKeyEvent", { type: "keyDown", key: "a", code: "KeyA", text: "a" });
    await sleep (20);
    t.check (await page.inView ("this.keyboard.keys.get (60).classList.contains ('down')"), "holding the A key holds C4");
    await page.send ("Input.dispatchKeyEvent", { type: "keyUp", key: "a", code: "KeyA" });
    const keyMIDI = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2].toString (16));
    t.check (keyMIDI[0] === "903c64" && keyMIDI[1] === "803c00", "the A key sends C4 on and off: " + keyMIDI);

    await screenshot (page, "06-final.png");

    // --- resizing
    await page.setSize (900, 700);
    await sleep (300);
    await screenshot (page, "07-small.png");
    const panel = await page.rectOf (".panel");
    t.check (Math.abs (panel.width - 900) < 2 && panel.top > 0, "the panel scales to fit the width: " + JSON.stringify (panel));

    await checkNoErrors (page, t, "while using the controls");
}

//==============================================================================
async function testNameEditing (guis, name, t)
{
    const page = await guis.open (name, size);
    const getState = () => page.inView ("({ line: this.lcd.lines[1], cursor: this.lcd.cursor?.column, editing: !! this.nameEdit })");
    const press = async (key, n = 1) => { for (let i = 0; i < n; ++i) await page.key (key, key.length === 1 ? (key === " " ? "Space" : /[a-z]/i.test (key) ? "Key" + key.toUpperCase() : key) : key); };
    const type = async (text) => { for (const ch of text) await press (ch); };
    const startEditing = async () => { const r = await page.rectOf ("#lcd-window"); await doubleClick (page, r.x, r.y); };

    await startEditing();
    let s = await getState();
    t.check (s.editing && s.line === "LatelyBass      " && s.cursor === 0, "name editing starts left-justified, with the cursor in column 1: " + JSON.stringify (s));

    await press ("ArrowRight", 20);
    s = await getState();
    t.check (s.cursor === 10, "the cursor can move to one past the 10th character: " + s.cursor);

    await press ("Backspace");
    s = await getState();
    t.check (s.line === "LatelyBas       " && s.cursor === 9, "backspace from past the end deletes the last letter: " + JSON.stringify (s));

    await press ("ArrowLeft", 20);
    await type ("ABCDEFGHIJ");
    s = await getState();
    t.check (s.line.startsWith ("ABCDEFGHIJ ") && s.cursor === 10, "typing 10 characters leaves the cursor past the end: " + JSON.stringify (s));

    await type ("M");
    s = await getState();
    t.check (s.line.startsWith ("ABCDEFGHIM ") && s.cursor === 10, "typing past the end replaces the 10th character: " + JSON.stringify (s));

    await press ("Backspace");
    s = await getState();
    t.check (s.line.startsWith ("ABCDEFGHI ") && s.cursor === 9, "backspace past the end deletes the 10th character: " + JSON.stringify (s));

    await press ("Backspace", 15);
    s = await getState();
    t.check (s.line.trim() === "" && s.cursor === 0, "backspace can clear the whole name: " + JSON.stringify (s));

    await type ("Hi");
    await press ("Enter");
    await sleep (50);
    s = await getState();
    t.check (! s.editing && await page.evaluate (`window.getStoredState ("voiceName")`) === "Hi" && (await getLCDText (page))[0] === "I01  Hi         ",
             "a short name is saved and shown");

    // Delete at the end does nothing, and Escape puts the old name back
    await startEditing();
    await press ("ArrowRight", 20);
    await press ("Delete");
    s = await getState();
    t.check (s.line.startsWith ("Hi ") && s.cursor === 10, "delete past the end is harmless: " + JSON.stringify (s));

    await type ("X");
    await press ("Escape");
    t.check ((await getLCDText (page))[0] === "I01  Hi         ", "escape keeps the old name");

    await checkNoErrors (page, t, "while editing the name");
}

//==============================================================================
async function testProgramButtons (guis, name, t, mode)
{
    // (the worker is slowed down, so that its replies to one click come after the next one)
    const page = await guis.open (name, { ...size, query: mode + "&workerLatency=150" });
    const buttons = await page.inView (`[...this.shadowRoot.querySelectorAll (".program-button")].map (b => { const r = b.getBoundingClientRect(); return { id: b.id, x: r.left + r.width / 2, y: r.top + r.height / 2 }; })`);
    const getPressedCount = () => page.inView (`this.shadowRoot.querySelectorAll (".program-button.pressed").length`);
    const next = buttons.find (b => b.id === "program-next"), previous = buttons.find (b => b.id === "program-previous");

    if (! t.check (buttons.length === 2 && next && previous, `${mode}: there are previous and next program buttons`))
        return;

    const bank = `import (window.pc.getResourceAddress ("gui/presets/presetBank.js"))`;
    const getExpectedIdleLine = (program) => page.evaluate (`${bank}.then (b => ("I" + String (${program}).padStart (2, "0") + "  " + b.getProgramName (${program})).padEnd (16))`);
    const getProgramValues = (program) => page.evaluate (`${bank}.then (b => b.getProgramParameterValues (${program}))`);

    // press and hold the next button
    await page.clearSent();
    await page.mouse ("mouseMoved", next.x, next.y);
    await page.mouse ("mousePressed", next.x, next.y, { buttons: 1 });
    t.check (await getPressedCount() === 1, `${mode}: a program button looks pressed while it's held`);
    await screenshot (page, `program-pressed-${mode}.png`, ".master");
    await page.mouse ("mouseReleased", next.x, next.y);
    t.check (await getPressedCount() === 0, `${mode}: it pops up when it's released`);

    // it sends a program change, which the worker answers by loading program 2
    await sleep (700);
    const midi = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2]);
    t.check (midi.length === 1 && midi[0] === 0xc00100, `${mode}: the next button sends a program change to program 2: ${midi.map (m => m.toString (16))}`);
    t.check (await page.evaluate (`window.getStoredState ("currentProgram")`) === 2, `${mode}: the worker loads program 2`);
    t.check ((await getLCDText (page))[0] === await getExpectedIdleLine (2), `${mode}: the LCD shows program 2 and its name: ${JSON.stringify (await getLCDText (page))}`);

    const values = await getProgramValues (2);
    const wrong = [];

    for (const [id, value] of Object.entries (values))
    {
        const patchValue = await getPatchParameterValue (page, id), controlValue = await getParameterValue (page, id);

        if (patchValue !== value || (controlValue !== undefined && controlValue !== value))
            wrong.push (`${id} should be ${value}: the patch has ${patchValue}, its control ${controlValue}`);
    }

    t.check (wrong.length === 0, `${mode}: the patch and the controls have program 2's values: ${wrong.join (", ") || "all right"}`);

    // press, slide off, and release elsewhere
    await page.clearSent();
    await page.mouse ("mouseMoved", previous.x, previous.y);
    await page.mouse ("mousePressed", previous.x, previous.y, { buttons: 1 });
    await page.mouse ("mouseMoved", previous.x + 200, previous.y + 200, { buttons: 1 });
    await page.mouse ("mouseReleased", previous.x + 200, previous.y + 200);
    await page.mouse ("mouseMoved", previous.x, previous.y);
    t.check (await getPressedCount() === 0, `${mode}: releasing away from a button doesn't leave it stuck down`);

    // clicking quickly, several times: each click steps once. The clicks are closer together
    // than the worker's latency, so its reply to each one arrives after the next, and those
    // late replies mustn't undo the clicks since
    await sleep (700);
    await page.clearSent();

    for (let i = 0; i < 5; ++i)
    {
        await page.click (next.x, next.y);
        await sleep (100);
    }

    await sleep (700);
    t.check (await getPressedCount() === 0, `${mode}: repeated clicks leave nothing pressed`);
    const steps = (await getSent (page)).filter (s => s[0] === "midi").map (s => ((s[2] >> 8) & 0x7f) + 1);
    const start = await page.inView ("this.currentProgram") - steps.length;
    t.check (steps.length === 5 && steps.every ((p, i) => p === start + i + 1), `${mode}: five clicks step through five programs: ${steps.join (", ")}`);
    t.check ((await getLCDText (page))[0] === await getExpectedIdleLine (steps.at (-1)), `${mode}: the LCD shows the last one: ${JSON.stringify (await getLCDText (page))}`);

    t.check (await page.inView ("this.shadowRoot.activeElement?.className ?? ''") !== "program-button", `${mode}: the buttons don't take the keyboard focus`);
    await checkNoErrors (page, t, `${mode}: with the program buttons`);
}

//==============================================================================
async function testReleasingOutsideControls (guis, name, t, mode)
{
    const page = await guis.open (name, { ...size, query: mode });
    const getValueMessageCount = async () => (await getSent (page)).filter (s => s[0] === "value").length;

    // 1. drag the level fader down past its end, release off the control, then move back over it
    let r = await page.rectOf (`[data-parameter="osc1LevelIn"] .slider-cap`);
    await page.mouse ("mouseMoved", r.x, r.y);
    await page.mouse ("mousePressed", r.x, r.y, { buttons: 1 });

    for (let y = r.y; y <= r.y + 330; y += 30)
        await page.mouse ("mouseMoved", r.x + 40, y, { buttons: 1 });

    await page.mouse ("mouseReleased", r.x + 40, r.y + 330);
    const levelAfterRelease = await getParameterValue (page, "osc1LevelIn");
    await page.clearSent();

    for (let y = r.y + 330; y >= r.y - 100; y -= 20)
        await page.mouse ("mouseMoved", r.x, y);

    t.check (levelAfterRelease === 0 && await getValueMessageCount() === 0 && await getParameterValue (page, "osc1LevelIn") === 0,
             `${mode}: releasing outside a fader ends its drag (level ${levelAfterRelease} -> ${await getParameterValue (page, "osc1LevelIn")})`);

    // 2. a knob, released off the knob, then hovered over again
    r = await page.rectOf (`[data-parameter="osc2FeedbackIn"] .knob-area`);
    await page.mouse ("mouseMoved", r.x, r.y);
    await page.mouse ("mousePressed", r.x, r.y, { buttons: 1 });

    for (let i = 1; i <= 8; ++i)
        await page.mouse ("mouseMoved", r.x + 10 * i, r.y + 15 * i, { buttons: 1 });

    await page.mouse ("mouseReleased", r.x + 80, r.y + 120);
    await page.clearSent();

    for (let i = 8; i >= -4; --i)
        await page.mouse ("mouseMoved", r.x, r.y + 15 * i);

    t.check (await getValueMessageCount() === 0, `${mode}: a knob's drag ends when it's released outside the knob`);

    // 3. the button released outside the window: there's no mouseup at all, and the next move has no buttons
    r = await page.rectOf (`[data-parameter="osc3LevelIn"] .slider-cap`);
    await page.mouse ("mouseMoved", r.x, r.y);
    await page.mouse ("mousePressed", r.x, r.y, { buttons: 1 });
    await page.mouse ("mouseMoved", r.x, r.y + 20, { buttons: 1 });
    await page.clearSent();
    await page.mouse ("mouseMoved", r.x, r.y - 40, { buttons: 0, button: "none" });
    await page.mouse ("mouseMoved", r.x, r.y - 80, { buttons: 0, button: "none" });
    const kinds = (await getSent (page)).map (s => s[0]);
    t.check (await getValueMessageCount() === 0 && kinds.includes ("gestureEnd"), `${mode}: a move with no button held ends the drag: ${kinds}`);

    // 4. the keyboard: glide off it and release, and the note must stop
    r = await page.rectOf (".keyboard");
    await page.mouse ("mouseMoved", r.left + 20, r.bottom - 20);
    await page.mouse ("mousePressed", r.left + 20, r.bottom - 20, { buttons: 1 });
    await page.mouse ("mouseMoved", r.left + 20, r.top - 60, { buttons: 1 });
    await page.mouse ("mouseReleased", r.left + 20, r.top - 60);
    await page.mouse ("mouseMoved", r.left + 20, r.bottom - 20);

    // (the session echoes MIDI back to the view, which lights keys too, so let that arrive)
    await sleep (50);
    t.check (await page.inView ("this.shadowRoot.querySelectorAll ('.key.down').length === 0 && this.keyboard.pointerNotes.size === 0"),
             `${mode}: no keys are stuck after releasing off the keyboard`);

    // 5. the pitch wheel, released outside it, springs back
    await page.clearSent();
    r = await page.rectOf (".wheel-housing");
    await page.mouse ("mouseMoved", r.x, r.y);
    await page.mouse ("mousePressed", r.x, r.y, { buttons: 1 });

    for (let i = 1; i <= 6; ++i)
        await page.mouse ("mouseMoved", r.x + 30 * i, r.y - 15 * i, { buttons: 1 });

    await page.mouse ("mouseReleased", r.x + 180, r.y - 90);
    await sleep (500);
    const lastMIDI = (await getSent (page)).filter (s => s[0] === "midi").map (s => s[2]).at (-1);
    t.check (lastMIDI === 0xe00040, `${mode}: the pitch wheel springs back after being released outside it: ${lastMIDI?.toString (16)}`);

    await checkNoErrors (page, t, `${mode}: when releasing outside the controls`);
}

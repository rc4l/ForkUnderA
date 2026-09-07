// UI interaction: keyboard navigation, mouse click/move/drag/wheel, text entry, and screenshot —
// ported from the old ZandronumMCP so the native system has full parity. Menus consume EV_GUI_Event
// (not raw key events); the bridge's input.event RPC carries {evtype,subtype,data1,data2} exactly.
//
// CRITICAL: every key DOWN must be paired with its UP. M_Responder latches a key on the way down and
// M_Ticker auto-repeats whatever stays latched, so a down without its release walks the whole menu.
import fs from "node:fs";
import path from "node:path";
import { parseHudDump, hudLines, findHudLabel } from "./proto.mjs";

// d_event.h / d_gui.h (EGUIEvent enum)
export const EV_GUI_EVENT = 4;
const SUB = {
  keydown: 1, keyup: 3, char: 4, mousemove: 6,
  ldown: 7, lup: 8, ldbl: 9,     // left
  mdown: 10, mup: 11, mdbl: 12,  // middle
  rdown: 13, rup: 14, rdbl: 15,  // right
  wheelup: 16, wheeldown: 17, wheelright: 18, wheelleft: 19,
};
// Per-button down/up/dblclick subtype triples.
const BTN = {
  left: { down: SUB.ldown, up: SUB.lup, dbl: SUB.ldbl },
  middle: { down: SUB.mdown, up: SUB.mup, dbl: SUB.mdbl },
  right: { down: SUB.rdown, up: SUB.rup, dbl: SUB.rdbl },
};
// [rc4l] pgup/pgdn/home/end/del are here because a menu now uses them: the Continue history is a
// list of up to fifty rows, and paging and jumping through it cannot be driven -- or asserted -- with
// only the arrows.
const GK = { up: 11, down: 10, left: 5, right: 6, enter: 13, back: 27, backspace: 8,
  pgdn: 1, pgup: 2, home: 3, end: 4, del: 26 };

// Raw (non-GUI) event types from d_event.h -- these carry keycodes, not GUI subtypes.
export const EV_KEYDOWN = 1;
export const EV_KEYUP = 2;
// Controller keycodes from doomdef.h. Buttons post as RAW EV_KeyDown/Up with data1=keycode and
// flow through the SAME keybind layer as the keyboard, so KEY_JOY1 does whatever `bind joy1 ...`
// maps it to. The physical A/B/X/Y ordering is device-specific (HID enumeration order), so we expose
// buttons by 1-based index (joy1..joy8) and the D-pad by direction (POV hat).
// NOTE: analog sticks/triggers are NOT here -- axes are hardware-polled each frame into joyaxes[]
// (IOKitJoystick::AddAxes -> G_BuildTiccmd), never queued as events, so they cannot be injected via
// input.event. Synthetic analog movement/aim belongs in a ticcmd primitive, not a fake gamepad.
const KEY_FIRSTJOYBUTTON = 0x108;
const JOY_POV = { up: 0x188, right: 0x189, down: 0x18a, left: 0x18b };
export function padButtonKeycode(n) {
  if (!Number.isInteger(n) || n < 1 || n > 8) throw new Error(`joystick button index out of range: ${n} (1..8)`);
  return KEY_FIRSTJOYBUTTON + (n - 1);
}
export function padDpadKeycode(dir) {
  const k = JOY_POV[dir];
  if (k == null) throw new Error(`unknown d-pad direction: ${dir} (up/down/left/right)`);
  return k;
}

// ---- Pure event builders (unit-tested) ------------------------------------
export function keyDownEvent(key) { return { evtype: EV_GUI_EVENT, subtype: SUB.keydown, data1: GK[key], data2: 0 }; }
export function keyUpEvent(key) { return { evtype: EV_GUI_EVENT, subtype: SUB.keyup, data1: GK[key], data2: 0 }; }
export function charEvent(ch) { return { evtype: EV_GUI_EVENT, subtype: SUB.char, data1: ch.charCodeAt(0), data2: 0 }; }
export function mouseMoveEvent(x, y) { return { evtype: EV_GUI_EVENT, subtype: SUB.mousemove, data1: x | 0, data2: y | 0 }; }
export function mouseButtonEvent(x, y, { button = "left", down = true, dbl = false } = {}) {
  const b = BTN[button];
  if (!b) throw new Error(`unknown mouse button: ${button} (left/middle/right)`);
  return { evtype: EV_GUI_EVENT, subtype: dbl ? b.dbl : (down ? b.down : b.up), data1: x | 0, data2: y | 0 };
}
export function wheelEvent(x, y, dir) {
  const sub = dir === "up" ? SUB.wheelup : dir === "down" ? SUB.wheeldown : dir === "right" ? SUB.wheelright : dir === "left" ? SUB.wheelleft : (dir > 0 ? SUB.wheelup : SUB.wheeldown);
  return { evtype: EV_GUI_EVENT, subtype: sub, data1: x | 0, data2: y | 0 };
}
export const isNavKey = (k) => Object.prototype.hasOwnProperty.call(GK, k);

// Controller button as a RAW key event (evtype 1/2, keycode in data1). subtype/data2 unused.
export function padButtonEvent(n, down = true) {
  return { evtype: down ? EV_KEYDOWN : EV_KEYUP, subtype: 0, data1: padButtonKeycode(n), data2: 0 };
}
export function padDpadEvent(dir, down = true) {
  return { evtype: down ? EV_KEYDOWN : EV_KEYUP, subtype: 0, data1: padDpadKeycode(dir), data2: 0 };
}

// ---- Client-driving helpers ------------------------------------------------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const post = (c, ev) => c.rpc("input.event", ev);

export async function menuNav(c, steps, { delay = 60 } = {}) {
  for (const s of steps) {
    if (!isNavKey(s)) throw new Error(`unknown nav key: ${s} (up/down/left/right/enter/back/backspace)`);
    await post(c, keyDownEvent(s));
    await post(c, keyUpEvent(s)); // always release, or it auto-repeats
    await sleep(delay);
  }
}

export async function click(c, x, y, { button = "left", double = false, delay = 40 } = {}) {
  await post(c, mouseMoveEvent(x, y)); // hover first, as a real pointer would
  await sleep(delay);
  await post(c, mouseButtonEvent(x, y, { button, down: true, dbl: double }));
  await sleep(delay);
  await post(c, mouseButtonEvent(x, y, { button, down: false }));
}
export const rightClick = (c, x, y, o = {}) => click(c, x, y, { ...o, button: "right" });
export const middleClick = (c, x, y, o = {}) => click(c, x, y, { ...o, button: "middle" });

export async function drag(c, x, y, toX, toY, { steps = 8, delay = 25 } = {}) {
  await post(c, mouseMoveEvent(x, y));
  await post(c, mouseButtonEvent(x, y, { down: true }));
  for (let i = 1; i <= steps; i++) {
    const ix = Math.round(x + (toX - x) * (i / steps));
    const iy = Math.round(y + (toY - y) * (i / steps));
    await post(c, mouseMoveEvent(ix, iy));
    await sleep(delay);
  }
  await post(c, mouseButtonEvent(toX, toY, { down: false }));
}

export async function wheel(c, x, y, dir, n = 1) { for (let i = 0; i < n; i++) await post(c, wheelEvent(x, y, dir)); }

// Controller: press a face/bumper button (joy1..joy8) or a D-pad direction, held `hold` ms then
// released. Fires through the keybind layer -- whatever the profile bound that joystick key to.
export async function padButton(c, n, { hold = 60 } = {}) {
  await post(c, padButtonEvent(n, true)); await sleep(hold); await post(c, padButtonEvent(n, false));
}
export async function padDpad(c, dir, { hold = 60 } = {}) {
  await post(c, padDpadEvent(dir, true)); await sleep(hold); await post(c, padDpadEvent(dir, false));
}

// Analog sticks: hold named axes (yaw/pitch/forward/side/up) at floats in [-1,1]. Unlike buttons,
// these do NOT go through the event queue -- input.axis installs a bridge-held override that
// G_BuildTiccmd stamps in each tic (a stick held at a position), running the real deadzone/accel
// pipeline. Deterministic and platform-agnostic. `stickClear` releases the override.
export const stick = (c, axes = {}) => c.rpc("input.axis", axes);
export const stickClear = (c) => c.rpc("input.axis", { clear: true });
// Push a stick to a position for `hold` ms, then recenter (release). e.g. stickHold(c,{forward:1}).
export async function stickHold(c, axes, { hold = 300 } = {}) {
  await stick(c, axes); await sleep(hold); await stickClear(c);
}

// Precise relative view rotation in DEGREES: yaw>0 = left, pitch>0 = down. Unlike the analog stick
// (a turn rate), this is an exact angular delta -- "look left 135 then down 10" = look(c,{yaw:135})
// then look(c,{pitch:10}). The view applies on the next tic, so in a paused sim step one tic after.
export const look = (c, { yaw = 0, pitch = 0 } = {}) => c.rpc("input.look", { yaw, pitch });
export async function typeText(c, text, { delay = 30 } = {}) { for (const ch of text) { await post(c, charEvent(ch)); await sleep(delay); } }

// Capture the frame via the engine's built-in `screenshot` CCMD (writes <name>.png beside the binary),
// then read it back. engineBinPath is FUACTL_ENGINE (the folder that also holds the IWAD/pk3).
export async function screenshot(c, engineBinPath, name = "fuactl_shot", { tries = 40, delay = 150 } = {}) {
  const dir = path.dirname(engineBinPath);
  const file = path.join(dir, `${name}.png`);
  try { fs.rmSync(file, { force: true }); } catch { /* ignore */ }
  await c.rpc("console.exec", { text: `screenshot ${name}` });
  for (let i = 0; i < tries; i++) { // wait for the PNG to land (a busy frame can take a moment)
    if (fs.existsSync(file) && fs.statSync(file).size > 0) return { path: file, base64: fs.readFileSync(file).toString("base64") };
    await sleep(delay);
  }
  throw new Error(`screenshot did not appear at ${file}`);
}

// Read the current menu/HUD as STRUCTURED TEXT instead of a screenshot: runs the engine's dumphud
// capture, collects the console output, and parses it. Returns { texts, images, msgs, lines }, where
// `lines` is the visible text in reading order. This is what lets navigation match labels
// ("Complex Doom", "Play Now") rather than guess pixels -- deterministic, no image reading.
// Teleport the (single-player) pawn to map coordinates -- no more blind steering loops.
export async function warp(c, x, y) {
  return c.rpc("player.setpos", { x: Math.round(x), y: Math.round(y) });
}

// The level's damaging sectors, each with a guaranteed-interior point to warp to.
export async function damagingSectors(c, limit = 64) {
  return c.rpc("world.sectors", { damaging: 1, limit });
}

export async function readMenu(c, { settle = 350 } = {}) {
  const out = [];
  const off = c.onEvent((n, d) => { if (n === "out" && d && d.text) out.push(d.text); });
  try {
    await c.rpc("console.exec", { text: "dumphud" });
    await sleep(settle);
  } finally { if (typeof off === "function") off(); }
  const parsed = parseHudDump(out.join(""));
  return { ...parsed, lines: hudLines(parsed.texts) };
}

// Where a label is on screen (its click anchor), or null. Convenience over readMenu + findHudLabel.
export async function findLabel(c, needle, opts) {
  return findHudLabel((await readMenu(c, opts)).texts, needle);
}

// Click a label by name: read the menu, find the label, and click its CENTRE (cx) -- the text-start
// alone can sit on a row's clickable edge and miss. Returns the label hit, or throws if not found.
export async function clickLabel(c, needle, { button = "left", double = false, delay = 40 } = {}) {
  const hit = findHudLabel((await readMenu(c)).texts, needle);
  if (!hit) throw new Error(`label not found on screen: ${needle}`);
  await click(c, hit.cx, hit.y, { button, double, delay });
  return hit;
}

// Open a menu, apply nav/click steps, screenshot to verify -- the old verify_menu, ported.
export async function verifyMenu(c, engineBinPath, { open, steps = [] } = {}) {
  if (open) await c.rpc("console.exec", { text: open });
  await sleep(300);
  await menuNav(c, steps);
  return screenshot(c, engineBinPath, "fuactl_verify");
}

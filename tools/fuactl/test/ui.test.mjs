import { test } from "node:test";
import assert from "node:assert/strict";
import {
  EV_GUI_EVENT, keyDownEvent, keyUpEvent, charEvent, mouseMoveEvent, mouseButtonEvent, wheelEvent, isNavKey,
  padButtonEvent, padDpadEvent, padButtonKeycode,
} from "../src/ui.mjs";

test("nav keys build EV_GUI keydown/up with the right GK codes", () => {
  assert.deepEqual(keyDownEvent("up"), { evtype: 4, subtype: 1, data1: 11, data2: 0 });
  assert.deepEqual(keyUpEvent("up"), { evtype: 4, subtype: 3, data1: 11, data2: 0 });
  assert.deepEqual(keyDownEvent("enter"), { evtype: 4, subtype: 1, data1: 13, data2: 0 });
  assert.deepEqual(keyDownEvent("back"), { evtype: 4, subtype: 1, data1: 27, data2: 0 }); // escape
  assert.equal(EV_GUI_EVENT, 4);
});

test("char event carries the character code (EV_GUI_Char=4)", () => {
  assert.deepEqual(charEvent("A"), { evtype: 4, subtype: 4, data1: 65, data2: 0 });
});

test("mouse move carries x/y", () => {
  assert.deepEqual(mouseMoveEvent(300, 63), { evtype: 4, subtype: 6, data1: 300, data2: 63 });
});

test("all three mouse buttons map to the correct subtypes", () => {
  assert.equal(mouseButtonEvent(1, 2, { button: "left", down: true }).subtype, 7);
  assert.equal(mouseButtonEvent(1, 2, { button: "left", down: false }).subtype, 8);
  assert.equal(mouseButtonEvent(1, 2, { button: "left", dbl: true }).subtype, 9);
  assert.equal(mouseButtonEvent(1, 2, { button: "middle", down: true }).subtype, 10);
  assert.equal(mouseButtonEvent(1, 2, { button: "middle", down: false }).subtype, 11);
  assert.equal(mouseButtonEvent(1, 2, { button: "right", down: true }).subtype, 13);   // <- right-click
  assert.equal(mouseButtonEvent(1, 2, { button: "right", down: false }).subtype, 14);
  assert.equal(mouseButtonEvent(1, 2, { button: "right", dbl: true }).subtype, 15);
});

test("wheel supports up/down/right/left", () => {
  assert.equal(wheelEvent(0, 0, "up").subtype, 16);
  assert.equal(wheelEvent(0, 0, "down").subtype, 17);
  assert.equal(wheelEvent(0, 0, "right").subtype, 18);
  assert.equal(wheelEvent(0, 0, "left").subtype, 19);
});

test("unknown button/nav key throws (no silent wrong-event)", () => {
  assert.throws(() => mouseButtonEvent(0, 0, { button: "x4" }));
  assert.equal(isNavKey("up"), true);
  assert.equal(isNavKey("pageup"), false);
});

test("controller buttons post RAW key events with KEY_JOY keycodes", () => {
  // joy1 = KEY_FIRSTJOYBUTTON (0x108); raw EV_KeyDown=1 / EV_KeyUp=2, subtype unused.
  assert.deepEqual(padButtonEvent(1, true), { evtype: 1, subtype: 0, data1: 0x108, data2: 0 });
  assert.deepEqual(padButtonEvent(8, false), { evtype: 2, subtype: 0, data1: 0x10f, data2: 0 });
  assert.equal(padButtonKeycode(3), 0x10a);
  assert.throws(() => padButtonKeycode(0));  // out of 1..8 range
  assert.throws(() => padButtonKeycode(9));
});

test("controller d-pad posts POV-hat keycodes", () => {
  assert.deepEqual(padDpadEvent("up", true), { evtype: 1, subtype: 0, data1: 0x188, data2: 0 });
  assert.deepEqual(padDpadEvent("left", false), { evtype: 2, subtype: 0, data1: 0x18b, data2: 0 });
  assert.throws(() => padDpadEvent("diagonal"));
});

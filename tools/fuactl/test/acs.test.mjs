import { test } from "node:test";
import assert from "node:assert/strict";
import { AcsScript, ACSF, acs0, acsScripts, callFunc, callSpecial, consoleCommand, gotoWord, hudMessage, incrementCVar, PCD, printString } from "../src/acs.mjs";

// [rc4l] Read an ACS0 lump back the way FBehavior does: directory at the header's offset, string table right after it.
function parse(buf) {
  assert.equal(buf.toString("latin1", 0, 4), "ACS\0");
  const dir = buf.readInt32LE(4);
  const n = buf.readInt32LE(dir);
  const scripts = [];
  for (let i = 0; i < n; i++) {
    const e = dir + 4 + 12 * i;
    scripts.push({ number: buf.readInt32LE(e), address: buf.readInt32LE(e + 4), args: buf.readInt32LE(e + 8) });
  }
  const table = dir + 4 + 12 * n;
  const strings = [];
  for (let i = 0; i < buf.readInt32LE(table); i++) {
    const at = buf.readInt32LE(table + 4 + 4 * i);
    strings.push(buf.toString("latin1", at, buf.indexOf(0, at)));
  }
  return { scripts, strings };
}

test("one OPEN script at the start of the code, and its strings in the table after the directory", () => {
  const buf = acs0([PCD.PUSHNUMBER, 7, PCD.TERMINATE], ["ONE", "TWO!"]);
  const { scripts, strings } = parse(buf);
  assert.deepEqual(scripts, [{ number: 1001, address: 8, args: 0 }]);
  assert.deepEqual(strings, ["ONE", "TWO!"]);
  assert.deepEqual([0, 1, 2].map((i) => buf.readInt32LE(8 + 4 * i)), [PCD.PUSHNUMBER, 7, PCD.TERMINATE]);
});

// [rc4l] WDI19's pool was tinted after the bake, and a template only controls that order when the harness runs the script itself.
test("a closed script is numbered without the OPEN thousand, so puke can run it", () => {
  const { scripts } = parse(acs0([PCD.TERMINATE], [], { open: false, number: 2 }));
  assert.deepEqual(scripts, [{ number: 2, address: 8, args: 0 }]);
});

test("several scripts lie end to end, each directory entry at its own code", () => {
  const buf = acsScripts([{ code: [PCD.PUSHNUMBER, 1, PCD.TERMINATE] }, { code: [PCD.TERMINATE], open: false, number: 2 }], ["S"]);
  const { scripts, strings } = parse(buf);
  assert.deepEqual(scripts, [{ number: 1001, address: 8, args: 0 }, { number: 2, address: 20, args: 0 }]);
  assert.deepEqual(strings, ["S"]);
  assert.equal(buf.readInt32LE(20), PCD.TERMINATE);
});

test("a special's arguments are pushed in order, then LSPECn names the special", () => {
  // [rc4l] Sector_SetPlaneReflection(125, 66, 0), as wdiexsec's OPEN script calls it.
  assert.deepEqual(callSpecial(159, [125, 66, 0]), [3, 125, 3, 66, 3, 0, 6, 159]);
  assert.deepEqual(callSpecial(80, [1]), [3, 1, 4, 80]);
  assert.throws(() => callSpecial(1, []), /1 to 5 arguments/);
  assert.throws(() => callSpecial(1, [1, 2, 3, 4, 5, 6]), /1 to 5 arguments/);
});

// [rc4l] Each p-code by its place in p_acs.h's enum, so a wrong number fails here rather than as a script that silently does something else.
test("every named p-code is numbered as p_acs.h's enum orders it", async () => {
  const { readFileSync } = await import("node:fs");
  const header = readFileSync(new URL("../../../src/zandronum/src/p_acs.h", import.meta.url), "latin1");
  const order = [...header.slice(header.indexOf("PCD_NOP")).matchAll(/\b(PCD_\w+)\s*,/g)].map((m) => m[1]);
  for (const [name, num] of Object.entries(PCD)) assert.equal(order.indexOf(`PCD_${name}`), num, name);
});

test("a print, a HUD message, a console command and a jump compile as the engine pops them", () => {
  assert.deepEqual(printString(2), [PCD.BEGINPRINT, PCD.PUSHNUMBER, 2, PCD.PRINTSTRING, PCD.ENDPRINT]);
  const hud = hudMessage(1, { id: 7, color: 6, x: 0.5, y: 0.25, hold: 2 });
  assert.deepEqual(hud.slice(0, 5), [PCD.BEGINPRINT, PCD.PUSHNUMBER, 1, PCD.PRINTSTRING, PCD.MOREHUDMESSAGE]);
  assert.deepEqual(hud.slice(5, -1).filter((_, k) => k % 2 === 1), [0, 7, 6, 32768, 16384, 131072]);
  assert.equal(hud[hud.length - 1], PCD.ENDHUDMESSAGE);
  assert.deepEqual(hudMessage(0).slice(5, -1).filter((_, k) => k % 2 === 1), [0, 0, 0, 32768, 32768, 65536]);
  assert.deepEqual(consoleCommand(3), [PCD.PUSHNUMBER, 3, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.CONSOLECOMMAND]);
  assert.deepEqual(gotoWord(5), [PCD.GOTO, 28]);
});

test("a fading HUD message takes its fade time after the optional-arguments code", () => {
  const hud = hudMessage(0, { type: 1, fade: 2 });
  assert.deepEqual(hud.slice(-4), [PCD.OPTHUDMESSAGE, PCD.PUSHNUMBER, 131072, PCD.ENDHUDMESSAGE]);
});

// [Doruk-Aksoy] SetCVar(s, GetCVar(s) + 1) with SetCVar's result dropped, so a script can count how often it ran.
test("incrementing a cvar reads it, adds one and sets it, leaving the stack as it was", () => {
  assert.deepEqual(incrementCVar(2), [PCD.PUSHNUMBER, 2, PCD.PUSHNUMBER, 2, PCD.GETCVAR, PCD.PUSHNUMBER, 1, PCD.ADD,
    PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP]);
});

test("an ACS function call pushes its arguments then CALLFUNC with their count and the function", () => {
  assert.deepEqual(callFunc(ACSF.SetCVar, [0, 5]), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 5, PCD.CALLFUNC, 2, ACSF.SetCVar]);
});

test("a script's named jump targets become byte offsets, and an unnamed one throws", () => {
  const s = new AcsScript().label("top").emit(PCD.PUSHNUMBER, 1).jump(PCD.IFGOTO, "end").jump(PCD.GOTO, "top").label("end").emit(PCD.TERMINATE);
  assert.deepEqual(s.words(), [PCD.PUSHNUMBER, 1, PCD.IFGOTO, 8 + 4 * 6, PCD.GOTO, 8, PCD.TERMINATE]);
  assert.throws(() => new AcsScript().jump(PCD.GOTO, "nowhere").words(), /no label nowhere/);
});

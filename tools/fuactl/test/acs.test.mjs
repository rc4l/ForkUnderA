import { test } from "node:test";
import assert from "node:assert/strict";
import { AcsScript, ACSF, acs0, acsScripts, addToCVar, callFunc, callSpecial, checksCounter, checksScript, failedChecks, consoleCommand, createTranslation, gotoWord, hudMessage, HUDMSG_ALPHA, incrementCVar, PCD, printString, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { judgeCounter } from "../src/template.mjs";

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

// [Doruk-Aksoy] ENTER's thousand in ACS0, and a flagged script making the lump ACSE: SPTR, SFLG and STRL as FBehavior reads them.
test("an ENTER script is numbered in the four thousands, an EVENT one in the sixteen thousands with its arguments", () => {
  const { scripts } = parse(acsScripts([{ code: [PCD.TERMINATE], type: SCRIPT.ENTER, number: 5 }]));
  assert.deepEqual(scripts, [{ number: 4005, address: 8, args: 0 }]);
  assert.deepEqual(parse(acsScripts([{ code: [PCD.TERMINATE], type: SCRIPT.EVENT, args: 3 }])).scripts, [{ number: 16001, address: 8, args: 3 }]);
});

test("a CLIENTSIDE script makes the lump ACSE, its flags in SFLG and the strings in STRL", () => {
  const buf = acsScripts([{ code: [PCD.PUSHNUMBER, 1, PCD.TERMINATE] }, { code: [PCD.TERMINATE], open: false, number: 7, flags: SCRIPTF.CLIENTSIDE }], ["AB", "C"]);
  assert.equal(buf.toString("latin1", 0, 4), "ACSE");
  const chunks = {};
  for (let at = buf.readInt32LE(4); at < buf.length; at += 8 + buf.readUInt32LE(at + 4))
    chunks[buf.toString("latin1", at, at + 4)] = buf.subarray(at + 8, at + 8 + buf.readUInt32LE(at + 4));
  assert.deepEqual(Object.keys(chunks), ["SPTR", "SFLG", "STRL"]);
  const sptr = chunks.SPTR;
  assert.deepEqual([sptr.readInt16LE(0), sptr.readUInt16LE(2), sptr.readInt32LE(4)], [1, SCRIPT.OPEN, 8]);
  assert.deepEqual([sptr.readInt16LE(12), sptr.readUInt16LE(14), sptr.readInt32LE(16)], [7, SCRIPT.CLOSED, 20]);
  assert.deepEqual([chunks.SFLG.readInt16LE(0), chunks.SFLG.readUInt16LE(2)], [7, SCRIPTF.CLIENTSIDE]);
  const strl = chunks.STRL;
  assert.equal(strl.readInt32LE(4), 2);
  const str = (i) => { const at = strl.readInt32LE(12 + 4 * i); return strl.toString("latin1", at, strl.indexOf(0, at)); };
  assert.deepEqual([str(0), str(1)], ["AB", "C"]);
  assert.equal(strl.length % 4, 0);
  assert.equal(buf.readInt32LE(20), PCD.TERMINATE);
});

test("a named script makes the lump ACSE, numbered from -1 in SPTR and SFLG with its name in SNAM", () => {
  const buf = acsScripts([{ code: [PCD.TERMINATE], open: false, number: 3 }, { code: [PCD.TERMINATE], open: false, name: "zxone", flags: SCRIPTF.NET }, { code: [PCD.TERMINATE], open: false, name: "zxtwo" }], ["S"]);
  assert.equal(buf.toString("latin1", 0, 4), "ACSE");
  const chunks = {};
  for (let at = buf.readInt32LE(4); at < buf.length; at += 8 + buf.readUInt32LE(at + 4))
    chunks[buf.toString("latin1", at, at + 4)] = buf.subarray(at + 8, at + 8 + buf.readUInt32LE(at + 4));
  assert.deepEqual(Object.keys(chunks), ["SPTR", "SFLG", "STRL", "SNAM"]);
  assert.deepEqual([0, 1, 2].map((i) => chunks.SPTR.readInt16LE(12 * i)), [3, -1, -2]);
  assert.deepEqual([chunks.SFLG.readInt16LE(0), chunks.SFLG.readUInt16LE(2)], [-1, SCRIPTF.NET]);
  const snam = chunks.SNAM;
  assert.equal(snam.readInt32LE(0), 2);
  const name = (i) => { const at = snam.readInt32LE(4 + 4 * i); return snam.toString("latin1", at, snam.indexOf(0, at)); };
  assert.deepEqual([name(0), name(1)], ["zxone", "zxtwo"]);
  assert.equal(snam.length % 4, 0);
  assert.equal(chunks.STRL.readInt32LE(4), 1, "STRL keeps its own layout");
  assert.ok(!Object.keys(chunks).includes("SNAM") || acsScripts([{ code: [PCD.TERMINATE], flags: SCRIPTF.NET }]).indexOf("SNAM") < 0, "no names, no SNAM");
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

// [rc4l] Each ACS function by its value in p_acs.cpp's EACSFunctions, counting on from each explicit number.
test("every named ACS function is numbered as p_acs.cpp's enum orders it", async () => {
  const { readFileSync } = await import("node:fs");
  const src = readFileSync(new URL("../../../src/zandronum/src/p_acs.cpp", import.meta.url), "latin1");
  const start = src.indexOf("enum EACSFunctions");
  const body = src.slice(src.indexOf("{", start) + 1, src.indexOf("};", start)).replace(/\/\/.*$/gm, "");
  const values = new Map();
  let next = 0;
  for (const m of body.matchAll(/\b(A[CS]{2}F_\w+)\s*(?:=\s*(\d+))?\s*,?/g)) {
    const v = m[2] !== undefined ? Number(m[2]) : next;
    values.set(m[1], v);
    next = v + 1;
  }
  assert.equal(values.get("ACSF_SetPlayerSkin"), 175);
  for (const [name, num] of Object.entries(ACSF)) assert.equal(values.get(`ACSF_${name}`), num, name);
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

// [Doruk-Aksoy] An alpha sets HUDMSG_ALPHA on the type and comes last of the optional arguments, after a fade.
test("a HUD message with an alpha flags its type and pushes the alpha after any fade", () => {
  const plain = hudMessage(0, { alpha: 0.75 });
  assert.equal(plain[6], HUDMSG_ALPHA);
  assert.deepEqual(plain.slice(-4), [PCD.OPTHUDMESSAGE, PCD.PUSHNUMBER, 49152, PCD.ENDHUDMESSAGE]);
  const fading = hudMessage(0, { type: 1, fade: 2, alpha: 0.5 });
  assert.equal(fading[6], 1 | HUDMSG_ALPHA);
  assert.deepEqual(fading.slice(-6), [PCD.OPTHUDMESSAGE, PCD.PUSHNUMBER, 131072, PCD.PUSHNUMBER, 32768, PCD.ENDHUDMESSAGE]);
});

// [Doruk-Aksoy] SetCVar(s, GetCVar(s) + 1) with SetCVar's result dropped, so a script can count how often it ran.
test("incrementing a cvar reads it, adds one and sets it, leaving the stack as it was", () => {
  assert.deepEqual(incrementCVar(2), [PCD.PUSHNUMBER, 2, PCD.PUSHNUMBER, 2, PCD.GETCVAR, PCD.PUSHNUMBER, 1, PCD.ADD,
    PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP]);
});

// [Doruk-Aksoy] A counter that a later script takes back, so it can say how many of something have not happened yet.
test("adding to a cvar adds any amount, a negative one taking away", () => {
  assert.deepEqual(addToCVar(1, -1), [PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 1, PCD.GETCVAR, PCD.PUSHNUMBER, -1, PCD.ADD,
    PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP]);
  assert.equal(acs0(addToCVar(0, -1)).readInt32LE(8 + 4 * 6), -1, "ACS0 keeps the sign");
});

test("an ACS function call pushes its arguments then CALLFUNC with their count and the function", () => {
  assert.deepEqual(callFunc(ACSF.SetCVar, [0, 5]), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 5, PCD.CALLFUNC, 2, ACSF.SetCVar]);
  assert.deepEqual(callFunc(ACSF.SetCVar, [[PCD.PUSHMAPVAR, 3], 5]), [PCD.PUSHMAPVAR, 3, PCD.PUSHNUMBER, 5, PCD.CALLFUNC, 2, ACSF.SetCVar]);
});

test("a script's named jump targets become byte offsets, and an unnamed one throws", () => {
  const s = new AcsScript().label("top").emit(PCD.PUSHNUMBER, 1).jump(PCD.IFGOTO, "end").jump(PCD.GOTO, "top").label("end").emit(PCD.TERMINATE);
  assert.deepEqual(s.words(), [PCD.PUSHNUMBER, 1, PCD.IFGOTO, 8 + 4 * 6, PCD.GOTO, 8, PCD.TERMINATE]);
  assert.throws(() => new AcsScript().jump(PCD.GOTO, "nowhere").words(), /no label nowhere/);
});

test("CreateTranslation pushes the number, then each range's four indices before its TRANSLATIONRANGE1, and ends the translation", () => {
  assert.deepEqual(createTranslation(2, [[0, 255, 112, 127], [16, 31, 176, 191]]), [PCD.PUSHNUMBER, 2, PCD.STARTTRANSLATION,
    PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 255, PCD.PUSHNUMBER, 112, PCD.PUSHNUMBER, 127, PCD.TRANSLATIONRANGE1,
    PCD.PUSHNUMBER, 16, PCD.PUSHNUMBER, 31, PCD.PUSHNUMBER, 176, PCD.PUSHNUMBER, 191, PCD.TRANSLATIONRANGE1, PCD.ENDTRANSLATION]);
});

// [rc4l] The bitmask puke tagmanager and udmfuserkeys judge maps by, one bit per check so a failure names which.
test("a checks puke counts 1 for running and adds each failing check's own bit, read by its kind", () => {
  const read = { light: (tag) => [PCD.PUSHNUMBER, tag, PCD.GETSECTORLIGHTLEVEL] };
  const words = checksScript([["a", "light", 5, 128], ["b", "light", 6, 96]], read);
  const at = (seq) => words.findIndex((_, i) => seq.every((w, j) => words[i + j] === w));
  assert.deepEqual(words.slice(0, addToCVar(0, 1).length), addToCVar(0, 1));
  assert.ok(at([PCD.PUSHNUMBER, 5, PCD.GETSECTORLIGHTLEVEL, PCD.PUSHNUMBER, 128, PCD.EQ, PCD.IFGOTO]) >= 0);
  assert.ok(at(addToCVar(0, 2)) >= 0 && at(addToCVar(0, 4)) >= 0);
  assert.equal(words.at(-1), PCD.TERMINATE);
  assert.deepEqual(checksScript([], read), [...addToCVar(0, 1), PCD.TERMINATE]);
});

test("a checks counter passes at 1 and names the failing checks, or the puke that never ran", () => {
  const checks = { MAP01: [["first"], ["second"], ["third"]] };
  assert.deepEqual(failedChecks(checks.MAP01, 1 + 2 + 8), ["first", "third"]);
  const counter = checksCounter("zx_c", (map) => checks[map]);
  assert.deepEqual(judgeCounter("MAP01", `"zx_c" is "1"`, counter), { line: `PASS MAP01 ${counter.label}: 1 (ceiling 1)`, failed: 0 });
  assert.match(judgeCounter("MAP01", `"zx_c" is "5"`, counter).line, /^FAIL MAP01 .*: second$/);
  assert.match(judgeCounter("MAP01", `"zx_c" is "0"`, counter).line, /: the puke never ran$/);
});

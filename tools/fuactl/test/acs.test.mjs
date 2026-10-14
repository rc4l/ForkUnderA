import { test } from "node:test";
import assert from "node:assert/strict";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

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

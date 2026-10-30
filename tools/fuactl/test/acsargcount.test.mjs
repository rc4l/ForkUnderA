import { test } from "node:test";
import assert from "node:assert/strict";
import { buildAcsArgCountPk3, buildScript, staleStack, COUNTER, HIDDEN } from "../scenarios/acsargcount.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

// [Doruk-Aksoy] The words each p-code the script uses takes after it.
const ARGS = { [PCD.PUSHNUMBER]: 1, [PCD.DELAYDIRECT]: 1, [PCD.IFNOTGOTO]: 1, [PCD.CALLFUNC]: 2 };

// [Doruk-Aksoy] Each CALLFUNC in the script as [argument count, function].
function calls(words) {
  const out = [];
  for (let i = 0; i < words.length; i += 1 + (ARGS[words[i]] ?? 0)) {
    if (words[i] === PCD.CALLFUNC) out.push([words[i + 1], words[i + 2]]);
  }
  return out;
}

test("staleStack leaves six copies past the stack top", () => {
  assert.deepEqual(staleStack(7), [PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 7,
    PCD.PUSHNUMBER, 7, PCD.DROP, PCD.DROP, PCD.DROP, PCD.DROP, PCD.DROP, PCD.DROP]);
});

test("the script calls SpawnForced without its angle, SpawnForced short of its z, and SpawnSpotForced with two arguments", () => {
  assert.deepEqual(calls(buildScript()).filter(([, fn]) => fn !== ACSF.SetCVar), [[5, ACSF.SpawnForced], [3, ACSF.SpawnForced], [2, ACSF.SpawnSpotForced]]);
});

test("each check fills the stack past its call with its own value, the one it then looks for", () => {
  const words = buildScript();
  for (const value of [192, 193, 194]) {
    const at = words.findIndex((w, i) => staleStack(value).every((s, k) => words[i + k] === s));
    assert.ok(at >= 0, `stale ${value}`);
  }
  // [Doruk-Aksoy] The short call and the spot call count things with the stale value as their TID.
  assert.ok(words.some((w, i) => w === PCD.PUSHNUMBER && words[i + 1] === 193 && words[i + 2] === PCD.THINGCOUNT));
  assert.ok(words.some((w, i) => w === PCD.PUSHNUMBER && words[i + 1] === 194 && words[i + 2] === PCD.THINGCOUNT));
});

test("the map carries the script, the counter and a map spot with the TID SpawnSpotForced names", () => {
  const bytes = buildAcsArgCountPk3();
  const text = bytes.toString("latin1");
  assert.match(text, /maps\/MAP01\.wad/);
  assert.match(text, /server int zx_acsargs_passed = 0;/);
  assert.match(text, new RegExp(`id = ${HIDDEN.spot};`));
  assert.equal(COUNTER.floor, 15);
});

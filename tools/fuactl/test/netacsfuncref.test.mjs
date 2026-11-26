import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { objectScripts, SCRIPTF } from "../src/acs.mjs";
import { COPY, START } from "../src/netacs.mjs";
import { MAPS, mergeLogs, buildNetFuncRefPk3 } from "../scenarios/netacsfuncref.mjs";

const fixture = (name) => objectScripts(fs.readFileSync(new URL(`../fixtures/acsfuncref/${name}.o`, import.meta.url)));

test("every step's script has its CLIENTSIDE copy in the compiled fixtures, and frefuse holds START", () => {
  const all = [...fixture("frefuse"), ...fixture("map02")];
  for (const { steps } of MAPS) for (const { script } of steps) {
    assert.ok(all.some((s) => s.number === script && !(s.flags & SCRIPTF.CLIENTSIDE)), `${script}`);
    assert.ok(all.some((s) => s.number === script + COPY && s.flags & SCRIPTF.CLIENTSIDE), `${script + COPY}`);
  }
  assert.ok(fixture("frefuse").some((s) => s.number === START));
});

test("each machine's output on the three maps merges by tag", () => {
  const a = { role: "player" }, b = { role: "server" };
  const merged = mergeLogs([new Map([[a, { map01: "1" }], [b, { map01: "2" }]]), new Map([[a, { map02: "3" }]])]);
  assert.deepEqual(merged.get(a), { map01: "1", map02: "3" });
  assert.deepEqual(merged.get(b), { map01: "2" });
});

test("the pk3 holds the three maps and the libraries", () => {
  const text = buildNetFuncRefPk3().toString("latin1");
  for (const name of ["maps/MAP01.wad", "maps/MAP02.wad", "maps/MAP03.wad", "acs/frefuse.o", "LOADACS.txt"]) assert.ok(text.includes(name), name);
});

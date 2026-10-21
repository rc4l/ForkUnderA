import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SPECIAL, THING, ENGINE_THING, SECTOR_TYPE } from "../src/doomnums.mjs";

const ENGINE = fileURLToPath(new URL("../../../src/zandronum/", import.meta.url));

// [rc4l] The names a MAPINFO file's DoomEdNums blocks number, "$"-prefixed special mapthings included.
function doomEdNums(file) {
  const found = {};
  const text = fs.readFileSync(path.join(ENGINE, "wadsrc/static/mapinfo", file), "latin1");
  for (const block of text.matchAll(/^\s*DoomEdNums\s*\{([^}]*)\}/gim))
    for (const m of block[1].matchAll(/^\s*(\d+)\s*=\s*"?(\$?\w+)"?/gm)) found[m[2]] = Number(m[1]);
  return found;
}

// [rc4l] Every actor's editor number as the engine resolves it: Doom's and the common DoomEdNums, then any a DECORATE actor still declares, which overrides them.
function editorNumbers() {
  const found = { ...doomEdNums("common.txt"), ...doomEdNums("doomitems.txt") };
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else if (e.name.endsWith(".txt"))
        for (const m of fs.readFileSync(p, "latin1").matchAll(/^\s*actor\s+(\w+)(?:\s*:\s*\w+)?\s+(\d+)/gim)) found[m[1]] = Number(m[2]);
    }
  };
  walk(path.join(ENGINE, "wadsrc/static/actors"));
  return found;
}

test("every named special has the number actionspecials.h gives it", () => {
  const header = fs.readFileSync(path.join(ENGINE, "src/actionspecials.h"), "latin1");
  for (const [name, num] of Object.entries(SPECIAL)) {
    const m = new RegExp(`DEFINE_SPECIAL\\(${name},\\s*(\\d+)`).exec(header);
    assert.ok(m, `${name} is not in actionspecials.h`);
    assert.equal(Number(m[1]), num, name);
  }
});

test("every named thing but PointLight has the editor number its DECORATE gives it", () => {
  const numbers = editorNumbers();
  for (const [name, num] of Object.entries(THING)) {
    if (name === "PointLight") continue;
    assert.equal(numbers[name], num, name);
  }
});

test("the polyobject anchor and spawn numbers are the common DoomEdNums' $PolyAnchor and $PolySpawn", () => {
  const common = doomEdNums("common.txt");
  assert.equal(ENGINE_THING.PolyAnchor, common.$PolyAnchor);
  assert.equal(ENGINE_THING.PolySpawn, common.$PolySpawn);
});

test("every named sector type has the number p_lnspec.h's enum gives it", () => {
  const header = fs.readFileSync(path.join(ENGINE, "src/p_lnspec.h"), "latin1");
  for (const [name, num] of Object.entries(SECTOR_TYPE)) {
    const m = new RegExp(`\\b${name}\\s*=\\s*(\\d+)`).exec(header);
    assert.ok(m, `${name} is not in p_lnspec.h`);
    assert.equal(Number(m[1]), num, name);
  }
});

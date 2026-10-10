import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SPECIAL, THING } from "../src/doomnums.mjs";

const ENGINE = fileURLToPath(new URL("../../../src/zandronum/", import.meta.url));

// [rc4l] Every DECORATE actor with an editor number, from the engine's own actor tree.
function editorNumbers() {
  const found = {};
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

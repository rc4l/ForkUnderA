import { test } from "node:test";
import assert from "node:assert/strict";
import { parseHudDump, hudLines, findHudLabel } from "../src/proto.mjs";

const SAMPLE = [
  "MCP_HUD",
  "image 0 0 TITLEPIC",
  "text 85 137 Popular",
  "text 141 137  Co-op Maps",
  "text 325 137 >",
  "text 396 247 Vanilla",
  "text 493 260 Complex Doom",
  "msg 0 0.500 0.900 35 You got the key",
  "",
].join("\n");

test("parseHudDump splits text/image/msg with numeric coords", () => {
  const { texts, images, msgs } = parseHudDump(SAMPLE);
  assert.equal(images.length, 1);
  assert.deepEqual(images[0], { x: 0, y: 0, name: "TITLEPIC" });
  assert.equal(texts.length, 5);
  assert.deepEqual(texts[0], { x: 85, y: 137, text: "Popular" });
  assert.deepEqual(texts.find((t) => t.text === "Complex Doom"), { x: 493, y: 260, text: "Complex Doom" });
  assert.equal(msgs.length, 1);
  assert.deepEqual(msgs[0], { layer: 0, left: 0.5, top: 0.9, tics: 35, text: "You got the key" });
});

test("parseHudDump ignores the header and blank/garbage lines", () => {
  const p = parseHudDump("MCP_HUD\n\nnonsense line\ntext 1 2 hi\n");
  assert.equal(p.texts.length, 1);
  assert.deepEqual(p.texts[0], { x: 1, y: 2, text: "hi" });
  assert.deepEqual(parseHudDump("").texts, []);
  assert.deepEqual(parseHudDump(null).images, []);
});

test("hudLines merges same-baseline fragments in reading order", () => {
  const { texts } = parseHudDump(SAMPLE);
  const lines = hudLines(texts);
  const first = lines.find((l) => l.text.startsWith("Popular"));
  assert.equal(first.text, "Popular Co-op Maps>");
  assert.equal(first.x, 85); // leftmost fragment anchors the line
  // vertical order preserved
  assert.ok(lines[0].y <= lines[lines.length - 1].y);
});

test("findHudLabel returns the exact fragment coords, not the merged line", () => {
  const { texts } = parseHudDump(SAMPLE);
  const cd = findHudLabel(texts, "complex doom");
  assert.deepEqual(cd, { x: 493, y: 260, text: "Complex Doom" });
  assert.equal(findHudLabel(texts, "nope"), null);
});

test("findHudLabel falls back to the merged line when no single fragment matches", () => {
  // "Co-op Maps" spans two fragments ("Popular" + " Co-op Maps"); the substring "Popular Co-op"
  // exists only on the merged line, so the fallback path is what answers.
  const { texts } = parseHudDump(SAMPLE);
  const hit = findHudLabel(texts, "popular co-op");
  assert.ok(hit && hit.text.includes("Co-op Maps"));
});

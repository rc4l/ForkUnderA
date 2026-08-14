import { test } from "node:test";
import assert from "node:assert/strict";
import { parseHudDump, hudLines, findHudLabel, summarizeGlTimers } from "../src/proto.mjs";

const SAMPLE = [
  "MCP_HUD",
  "image 0 0 TITLEPIC",
  "text 85 137 60 Popular",
  "text 141 137 90  Co-op Maps",
  "text 325 137 8 >",
  "text 396 247 56 Vanilla",
  "text 493 260 96 Complex Doom",
  "msg 0 0.500 0.900 35 You got the key",
  "",
].join("\n");

test("parseHudDump splits text/image/msg with numeric coords + width", () => {
  const { texts, images, msgs } = parseHudDump(SAMPLE);
  assert.equal(images.length, 1);
  assert.deepEqual(images[0], { x: 0, y: 0, name: "TITLEPIC" });
  assert.equal(texts.length, 5);
  assert.deepEqual(texts[0], { x: 85, y: 137, w: 60, text: "Popular" });
  assert.deepEqual(texts.find((t) => t.text === "Complex Doom"), { x: 493, y: 260, w: 96, text: "Complex Doom" });
  assert.equal(msgs.length, 1);
  assert.deepEqual(msgs[0], { layer: 0, left: 0.5, top: 0.9, tics: 35, text: "You got the key" });
});

test("parseHudDump ignores the header and blank/garbage lines", () => {
  const p = parseHudDump("MCP_HUD\n\nnonsense line\ntext 1 2 10 hi\n");
  assert.equal(p.texts.length, 1);
  assert.deepEqual(p.texts[0], { x: 1, y: 2, w: 10, text: "hi" });
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

test("findHudLabel returns the fragment coords + centre (cx), not the merged line", () => {
  const { texts } = parseHudDump(SAMPLE);
  const cd = findHudLabel(texts, "complex doom");
  assert.deepEqual(cd, { x: 493, y: 260, w: 96, text: "Complex Doom", cx: 493 + 48 });
  assert.equal(findHudLabel(texts, "nope"), null);
});

test("findHudLabel falls back to the merged line when no single fragment matches", () => {
  // "Co-op Maps" spans two fragments ("Popular" + " Co-op Maps"); the substring "Popular Co-op"
  // exists only on the merged line, so the fallback path is what answers.
  const { texts } = parseHudDump(SAMPLE);
  const hit = findHudLabel(texts, "popular co-op");
  assert.ok(hit && hit.text.includes("Co-op Maps"));
});

test("summarizeGlTimers ranks passes hottest-first and appends draw counters", () => {
  const report = {
    available: true, frames: 117,
    total: { mean_ms: 15.8 },
    zones: { scene: { mean_ms: 4.1 }, translucent: { mean_ms: 10.8 }, hud2d: { mean_ms: 0.9 } },
    counters: { walls: 812, flats: 190, sprites: 44, vertices: 30500 },
  };
  const line = summarizeGlTimers(report);
  assert.match(line, /GPU 15\.80ms\/frame over 117 frames/);
  // translucent (10.8) is the hottest, so it must come before scene (4.1) before hud2d (0.9)
  assert.ok(line.indexOf("translucent 10.80") < line.indexOf("scene 4.10"));
  assert.ok(line.indexOf("scene 4.10") < line.indexOf("hud2d 0.90"));
  assert.match(line, /\[812w 190f 44s 30500v\]/);
});

test("summarizeGlTimers reports unavailable with and without a note", () => {
  assert.equal(
    summarizeGlTimers({ available: false, note: "driver returned zero" }),
    "GPU timing unavailable (driver returned zero)");
  assert.equal(summarizeGlTimers({ available: false }), "GPU timing unavailable");
  assert.equal(summarizeGlTimers(null), "GPU timing unavailable"); // defensive: no report at all
});

test("summarizeGlTimers tolerates missing zones/total/counters", () => {
  const line = summarizeGlTimers({ available: true, frames: 3, total: {}, zones: {} });
  assert.equal(line, "GPU ?ms/frame over 3 frames"); // no zones, no counters, unknown mean -> "?"
});

test("summarizeGlTimers handles a non-numeric zone and sparse counters defensively", () => {
  const line = summarizeGlTimers({
    available: true, // frames omitted -> defaults to 0
    total: { mean_ms: 2 },
    zones: { scene: { mean_ms: 1.0 }, broken: {} }, // broken has no mean_ms -> ranked last, printed "?"
    counters: { walls: 5 },                          // flats/sprites/vertices missing -> 0
  });
  assert.match(line, /over 0 frames/);
  assert.ok(line.indexOf("scene 1.00") < line.indexOf("broken ?")); // numeric pass ranks above the unknown
  assert.match(line, /\[5w 0f 0s 0v\]/);
});

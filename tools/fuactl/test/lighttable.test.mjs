import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildLightTableWad, compareEachSetting, ROOMS, SETTINGS, VANTAGES } from "../scenarios/lighttable.mjs";
import { judgeLightTableSends, meanOf, SENT_CEILING } from "../scenarios/lighttablesends.mjs";

test("MAP01 holds every light table room and MAP02 all but the 3D floor's, each with the script that changes them", () => {
  const lumps = readWad(buildLightTableWad()).map((l) => ({ name: l.name, text: l.data.toString("latin1") }));
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "BEHAVIOR", "ENDMAP", "MAP02", "TEXTMAP", "BEHAVIOR", "ENDMAP"]);
  const sectors = (t) => (t.match(/^sector/gm) || []).length;
  // [rc4l] Each room is a sector with its dark corner block, and the 3D floor adds its control sector.
  assert.equal(sectors(lumps[1].text), ROOMS.length * 2 + 1);
  assert.equal(sectors(lumps[5].text), (ROOMS.length - 1) * 2);
  assert.equal(VANTAGES.length, ROOMS.length);
});

test("compareEachSetting runs the comparison under every setting and names its lines by it", async () => {
  const asked = [];
  const r = await compareEachSetting({ name: "lt" }, async (o) => {
    asked.push([o.name, o.commands]);
    return { lines: ["PASS view"], failed: o.commands.length ? 1 : 0, refused: o.name === "lt_radialfog" };
  });
  assert.deepEqual(asked.map((a) => a[0]), ["lt_lightmode3", "lt_lightmode0", "lt_lightmode8", "lt_radialfog"]);
  assert.deepEqual(asked.map((a) => a[1]), SETTINGS.map((s) => s.commands));
  assert.deepEqual(r.lines, SETTINGS.map((s) => `${s.name}: PASS view`));
  assert.equal(r.failed, 3);
  assert.equal(r.refused, true);
});

test("light changes the table carries pass, and no tic, no change or records sent again fail", () => {
  assert.equal(judgeLightTableSends("x", { shading: 8, sent: 0, tics: 280 }).failed, 0);
  assert.match(judgeLightTableSends("x", { shading: 8, sent: 0, tics: 0 }).line, /no tic ran/);
  assert.match(judgeLightTableSends("x", { shading: 0, sent: 0, tics: 280 }).line, /did not change/);
  assert.match(judgeLightTableSends("x", { shading: 8, sent: 30, tics: 280 }).line, /30\.0 surface records sent a frame/);
  assert.equal(judgeLightTableSends("x", { shading: 8, sent: SENT_CEILING, tics: 280 }).failed, 0);
});

test("meanOf reads a counter's mean and 0 for one never counted", () => {
  assert.equal(meanOf([{ name: "gpu/repaint_sent", mean: "0.25" }], "gpu/repaint_sent"), 0.25);
  assert.equal(meanOf([], "gpu/repaint_sent"), 0);
});

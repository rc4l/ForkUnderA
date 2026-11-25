// [Doruk-Aksoy] netgpusprites' map, its glow reading and its judge, on synthetic values.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildNetGpuSpritesWad, CORPSES, parseGlowLeft, judgeStill } from "../scenarios/netgpusprites.mjs";
import { readWad, parseUdmf } from "../src/udmf.mjs";

const stats = (o = {}) => ({ pieces: 20, packed: 20, unpacked: 0, hidden: 0, shown: CORPSES.length * 2, frameDerived: 0, cameraTicDraws: 0, builds: 1, fill: 1, ...o });

test("the map holds a row of corpses on the plain floor and one on the glowing flat, which GLDEFS lights", () => {
  const lumps = readWad(buildNetGpuSpritesWad());
  assert.match(lumps.find((l) => l.name === "GLDEFS").data.toString(), /FLAT14/);
  const parsed = parseUdmf(lumps.find((l) => l.name === "TEXTMAP").data.toString("latin1"));
  const things = parsed.things || parsed.thing;
  assert.equal(things.filter((t) => CORPSES.includes(t.type)).length, CORPSES.length * 2);
  assert.ok((parsed.sectors || parsed.sector).some((s) => s.texturefloor === "FLAT14"));
});

test("the glow count is read from ticwhy, and none when the line is missing", () => {
  assert.equal(parseGlowLeft("ticstats pieces 9\nticwhy never 1 nosubject 0 playerbob 0 model 0 shape 0 sector 0 shader 0 clip 0 glow 9 shadowfog 0 lane 0 pieces 0"), 9);
  assert.equal(parseGlowLeft("Unknown command"), null);
});

test("a moment passes only with the same picture, every corpse a tic sprite and none on the glow left to the frame", () => {
  assert.deepEqual(judgeStill({ worst: 0, stats: stats(), glowLeft: 0 }, "on joining"), []);
  assert.equal(judgeStill({ worst: 9, stats: stats(), glowLeft: 0 }, "x").length, 1);
  assert.equal(judgeStill({ worst: 0, stats: null, glowLeft: 0 }, "x").length, 1);
  assert.equal(judgeStill({ worst: 0, stats: stats({ shown: 9 }), glowLeft: 0 }, "x").length, 1);
  // [Doruk-Aksoy] The build before steady actors left the nine corpses on the glow to the frame.
  assert.match(judgeStill({ worst: 0, stats: stats({ shown: 9 }), glowLeft: 9 }, "after its reconnect").join("; "), /9 corpses on the glowing floor/);
  assert.equal(judgeStill({ worst: 0, stats: stats(), glowLeft: null }, "x").length, 1);
});

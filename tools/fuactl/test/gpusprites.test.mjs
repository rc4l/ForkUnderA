// [Doruk-Aksoy] gpusprites' maps, its ticstats reading and its judges, on synthetic values.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildGpuSpritesWad, MAPS, SETTINGS, CAMERA_TID, CEILING, parseTicStats, worstBlocks, judgeFractions, judgeSetting, judgeRun } from "../scenarios/gpusprites.mjs";
import { readWad, parseUdmf } from "../src/udmf.mjs";

const stats = (o = {}) => ({ pieces: 3, packed: 3, unpacked: 0, hidden: 0, shown: 3, frameDerived: 0, cameraTicDraws: 0, builds: 0, fill: 100, ...o });

test("the wad holds every map, the camera screen's ANIMDEFS, and MAP05's camera, script and floating cacodemons", () => {
  const lumps = readWad(buildGpuSpritesWad());
  for (const map of Object.keys(MAPS)) assert.ok(lumps.some((l) => l.name === map), map);
  assert.match(lumps.find((l) => l.name === "ANIMDEFS").data.toString(), /cameratexture GPUCAM/);
  const at = lumps.findIndex((l) => l.name === "MAP05");
  const textmap = lumps.slice(at).find((l) => l.name === "TEXTMAP");
  const parsed = parseUdmf(textmap.data.toString("latin1"));
  const things = parsed.things || parsed.thing;
  assert.equal(things.filter((t) => t.type === 9001 && t.id === CAMERA_TID).length, 1);
  assert.ok(things.filter((t) => t.type === 3005).every((t) => t.dormant && t.height > 0));
  assert.ok(lumps.slice(at).some((l) => l.name === "BEHAVIOR"));
  for (const def of Object.values(MAPS)) for (const name of def.settings || []) assert.ok(SETTINGS[name], name);
});

test("parseTicStats reads every field of fua_dg_ticstats's line, and null when it printed none", () => {
  assert.deepEqual(parseTicStats("ticstats pieces 5 packed 4 unpacked 1 hidden 2 shown 3 framederived 7 cameraticdraws 0 builds 9 fill 412"),
    { pieces: 5, packed: 4, unpacked: 1, hidden: 2, shown: 3, frameDerived: 7, cameraTicDraws: 0, builds: 9, fill: 412 });
  assert.equal(parseTicStats("Unknown command"), null);
});

test("worstBlocks ranks 16-pixel blocks by mean difference", () => {
  const pic = (fill) => ({ w: 32, h: 16, channels: 3, data: Buffer.alloc(32 * 16 * 3, fill) });
  const b = pic(10);
  for (let y = 0; y < 16; y++) for (let x = 16; x < 32; x++) b.data[(y * 32 + x) * 3] = 40;
  const w = worstBlocks(pic(10), b, 2);
  assert.deepEqual(w.map((x) => [x.x, Math.round(x.d)]), [[16, 10], [0, 0]]);
});

test("judgeFractions fails a pair over the ceiling and one the shader path drew nothing for", () => {
  const ok = { worst: [{ d: 0 }], stats: stats() };
  assert.equal(judgeFractions([ok]).pass, true);
  assert.equal(judgeFractions([ok, { worst: [{ d: CEILING + 1 }], stats: stats() }]).failed.length, 1);
  assert.equal(judgeFractions([{ worst: [{ d: 0 }], stats: stats({ shown: 0 }) }]).pass, false);
});

test("judgeSetting passes the same pictures only when the change moved the picture", () => {
  assert.equal(judgeSetting({ worst: [{ d: 0 }], moved: 30, stats: stats() }), true);
  assert.equal(judgeSetting({ worst: [{ d: 40 }], moved: 30, stats: stats() }), false);
  assert.equal(judgeSetting({ worst: [{ d: 0 }], moved: 0, stats: stats() }), false);
  assert.equal(judgeSetting({ worst: [{ d: 0 }], moved: 30, stats: null }), false);
});

test("judgeRun holds the stream to one make a fill and no tic run in a camera pass", () => {
  assert.equal(judgeRun(stats({ fill: 100 }), stats({ fill: 150, builds: 51 })).ok, true);
  assert.match(judgeRun(stats({ fill: 100 }), stats({ fill: 150, builds: 400 })).why, /made 400 times over 50 fills/);
  assert.match(judgeRun(stats({ fill: 100 }), stats({ fill: 150, builds: 50, cameraTicDraws: 7 })).why, /camera passes drew 7/);
  assert.match(judgeRun(stats({ fill: 100 }), stats({ fill: 104, builds: 4 })).why, /only 4 fills/);
  assert.equal(judgeRun(null, stats()).ok, false);
});

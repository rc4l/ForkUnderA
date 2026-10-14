import { test } from "node:test";
import assert from "node:assert/strict";
import { perfSchedule, perfVerdicts, settingNames, cvarValueOf, isolatedLines } from "../src/abperf.mjs";

// [rc4l] Queue #42's cost was found by switching settings inside one session, after separate launches drifted 30%.
test("every setting is measured once per round, so drift falls on all of them", () => {
  assert.deepEqual(perfSchedule(["a", "b"], 2), [
    { round: 1, setting: "a" }, { round: 1, setting: "b" }, { round: 2, setting: "a" }, { round: 2, setting: "b" },
  ]);
  assert.deepEqual(perfSchedule(["a"], 0), []);
});

// [rc4l] The live run that needed pairing: round 1 ran 1.4 ms slower throughout, which unpaired samples read as noise.
test("a setting is judged against the first within each round, and only a difference every round agrees on counts", () => {
  const rows = [
    { round: 1, setting: "fua_gl_meshdraw 1", p50: 5.004 }, { round: 1, setting: "fua_gl_meshdraw 0", p50: 3.524 }, { round: 1, setting: "x", p50: 5.5 },
    { round: 2, setting: "fua_gl_meshdraw 1", p50: 3.613 }, { round: 2, setting: "fua_gl_meshdraw 0", p50: 2.315 }, { round: 2, setting: "x", p50: 3.0 },
  ];
  const [draw, x] = perfVerdicts(rows);
  assert.deepEqual(draw, { label: "fua_gl_meshdraw 0 against fua_gl_meshdraw 1", verdict: "faster in every round",
    deltas: [-1.48, -1.298], median: -1.389 });
  assert.equal(x.verdict, "no consistent difference");
  assert.equal(perfVerdicts([{ round: 1, setting: "a", p50: 1 }, { round: 1, setting: "b", p50: 2 }])[0].verdict, "slower in every round");
  assert.equal(perfVerdicts([{ round: 1, setting: "a", p50: 1 }, { round: 2, setting: "b", p50: 2 }])[0].verdict, "insufficient data");
  assert.deepEqual(perfVerdicts([{ round: 1, setting: "only", p50: 1 }]), []);
});

// [rc4l] The first live run left gl_wallmesh 0 on after round 1, so every later round measured the cache switched off.
test("each setting runs with the other settings' cvars put back where they started", () => {
  assert.deepEqual(settingNames("fua_gl_meshdraw 0; gl_wallmesh 0"), ["fua_gl_meshdraw", "gl_wallmesh"]);
  assert.deepEqual(settingNames("stat rendertimes; bench"), ["stat"]);
  const settings = ["fua_gl_meshdraw 1", "fua_gl_meshdraw 0", "gl_wallmesh 0", "echo base"];
  assert.deepEqual(isolatedLines(settings, { fua_gl_meshdraw: "false", gl_wallmesh: "true", echo: null }), [
    "gl_wallmesh true; fua_gl_meshdraw 1",
    "gl_wallmesh true; fua_gl_meshdraw 0",
    "fua_gl_meshdraw false; gl_wallmesh 0",
    "fua_gl_meshdraw false; gl_wallmesh true; echo base",
  ]);
});

test("a cvar's starting value comes from the console's reply, and anything else is not a cvar", () => {
  assert.equal(cvarValueOf('"gl_wallmesh" is "true"', "gl_wallmesh"), "true");
  assert.equal(cvarValueOf("", "echo"), null);
  assert.equal(cvarValueOf('"gl_wallmeshx" is "1"', "gl_wallmesh"), null);
  assert.equal(cvarValueOf('"a.b" is "1"', "a.b"), null);
});

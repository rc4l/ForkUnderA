import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { PROBE, SAMPLES, CONFIGS, parseProbe, judgeSamples, judgeNetplay, buildUnlaggedPk3 } from "../scenarios/netacsunlagged.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";

test("a probe's log line reads as its three answers, and anything else as none", () => {
  assert.deepEqual(parseProbe("junk\nunl 0 1 1\n"), [0, 1, 1]);
  assert.equal(parseProbe("nothing here"), null);
  assert.equal(parseProbe(undefined), null);
});

test("a strafing target must dodge the rewound trace alone, and a still one be hit by all three", () => {
  assert.deepEqual(judgeSamples([[0, 1, 1], [0, 1, 1]], true), []);
  assert.deepEqual(judgeSamples([[1, 1, 1]], false), []);
  assert.match(judgeSamples([[1, 1, 1]], true).join(), /rewound trace hit the target where it stands/);
  assert.match(judgeSamples([[0, 1, 1]], false).join(), /rewound trace missed a target that stood still/);
  assert.match(judgeSamples([[0, 0, 1]], true).join(), /TRF_FUA_NOUNLAGGED missed/);
  assert.match(judgeSamples([[0, 1, 0]], true).join(), /MapSpot's trace missed/);
  assert.match(judgeSamples([null], true).join(), /sample 0 logged nothing/);
});

test("the strafing config holds a direction through each probe and turns between them; the still one never moves", async () => {
  const run = async (config, answer) => {
    const sent = [];
    const set = { server: { c: "server" }, clients: [{ c: "shooter" }, { c: "target" }] };
    const deps = { exec: async (c, text) => sent.push(`${c}:${text}`), sleep: async () => sent.push("sleep"), waitTics: async () => {},
      take: async () => { sent.push("probe"); return answer; } };
    return { r: await judgeNetplay(set, "MAP01 x", { config }, deps), sent };
  };
  const strafe = await run({ strafe: true }, [0, 1, 1]);
  assert.equal(strafe.r.failed, 0);
  assert.deepEqual(strafe.sent.slice(0, 8), ["target:+moveright", "sleep", "probe", "target:-moveright", "target:+moveleft", "sleep", "probe", "target:-moveleft"]);
  assert.equal(strafe.sent.filter((s) => s === "probe").length, SAMPLES);
  const still = await run({}, [1, 1, 1]);
  assert.equal(still.r.failed, 0);
  assert.deepEqual(still.sent, Array(SAMPLES).fill("probe"));
  assert.equal((await run({ strafe: true }, [1, 1, 1])).r.failed, 1);
});

test("both configs run their server on snapshots, the lagged one strafing", () => {
  assert.deepEqual(CONFIGS.map((c) => [c.name, c.players, !!c.strafe, c.lag]), [["lan", 2, false, undefined], ["ping200", 2, true, "200"]]);
  for (const c of CONFIGS) assert.deepEqual(c.serverCvars, SNAP_SERVER_CVARS);
});

test("the pk3 builds with the compiled probe beside its source", () => {
  assert.ok(fs.existsSync(PROBE) && fs.existsSync(PROBE.replace(/\.o$/, ".bcs")));
  assert.ok(buildUnlaggedPk3().length > fs.statSync(PROBE).size);
});

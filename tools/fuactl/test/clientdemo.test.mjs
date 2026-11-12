import { test } from "node:test";
import assert from "node:assert/strict";
import { travel, judgeTravel, judgeAfterSkip, SKIP_PACE_MAX, secondsLeft, judgeCountdown, buildClientDemoPk3, CONFIGS, DEMO, MAPINFO } from "../scenarios/clientdemo.mjs";

const at = (tic, y) => ({ tic, actors: [{ player: 1, x: 96, y }, { player: 2, x: 160, y: 0 }] });

test("travel sums a player's path and its mean pace a tic", () => {
  const r = travel([at(10, 0), at(11, 10), at(12, 10), at(14, 60), at(15, 70)], 1);
  assert.deepEqual(r, { samples: 5, total: 70, pace: 14 });
  assert.deepEqual(travel([at(10, 0)], 1), { samples: 1, total: 0, pace: 0 });
  assert.deepEqual(travel([at(10, 0)], 3), { samples: 0, total: 0, pace: 0 });
});

test("the playback passes a running player and names a still or missing one", () => {
  assert.deepEqual(judgeTravel({ samples: 60, total: 1500, pace: 16 }), []);
  assert.deepEqual(judgeTravel({ samples: 60, total: 0, pace: 0 }), ["the runner went 0.0 units in the playback"]);
  assert.deepEqual(judgeTravel({ samples: 60, total: 101, pace: 0 }), ["the runner went 101.0 units in the playback"], "a spawn's jump is not a run");
  assert.deepEqual(judgeTravel({ samples: 3, total: 1500, pace: 16 }), ["the playback held the runner in only 3 samples"]);
});

test("the observer records to the demo path, and the pk3 holds the map", () => {
  for (const config of CONFIGS) assert.deepEqual(config.clientExtraArgs, [[], ["-record", DEMO]]);
  assert.deepEqual(CONFIGS.map((c) => c.skipTics), [undefined, 35]);
  assert.match(MAPINFO, /next = "MAP02"/);
  for (const name of ["maps/MAP01.wad", "maps/MAP02.wad", "mapinfo.txt"]) assert.ok(buildClientDemoPk3().toString("latin1").includes(name), name);
});

test("the countdown is read from the board's line, and must hold while paused", () => {
  assert.equal(secondsLeft([{ text: "Rankings" }, { text: "Entering MAP02: After in 7 seconds" }]), 7);
  assert.equal(secondsLeft([{ text: "Entering MAP02: After in 1 second" }]), 1);
  assert.equal(secondsLeft([{ text: "Rankings" }]), null);
  assert.deepEqual(judgeCountdown(7, 7), []);
  assert.deepEqual(judgeCountdown(7, 5), ["the paused playback's countdown went from 7 to 5 seconds"]);
  assert.deepEqual(judgeCountdown(null, 5), ["the playback's intermission showed no countdown"]);
});

test("after a skip the runner must go at a runner's pace, not a backlog's", () => {
  assert.deepEqual(judgeAfterSkip({ samples: 40, total: 600, pace: 16.7 }), []);
  assert.deepEqual(judgeAfterSkip({ samples: 40, total: 1200, pace: 33.4 }), ["the runner caught up on a backlog after the skip, 33.4 units a tic"]);
  assert.deepEqual(judgeAfterSkip({ samples: 40, total: 0, pace: 0 }), ["the runner stood still after the skip, 0.0 units a tic"]);
  assert.deepEqual(judgeAfterSkip({ samples: 3, total: 50, pace: 16 }), ["the playback held the runner in only 3 samples after the skip"]);
  assert.ok(SKIP_PACE_MAX > 16.7 && SKIP_PACE_MAX < 33.4);
});

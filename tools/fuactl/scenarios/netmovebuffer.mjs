// [rc4l] The client's move buffers (zandronum@93c67c50fda9): exit 0 when an observer whose packets arrive in bursts sees a running player step unevenly from tic to tic at most half as often with cl_usemovebuffer on as with it off, switched in one session.
//
// usage:  node scenarios/netmovebuffer.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

// [rc4l] A corridor long enough to run up for the whole sample, the runner's start ahead of the observer's.
export function buildNetMoveBufferPk3() {
  const m = new UdmfMap();
  m.room(0, -256, 256, 8192, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 96, y: 64, angle: 90, type: 1 });
  m.thing({ x: 160, y: -128, angle: 90, type: 2 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] Tics the runner runs before the sample, to reach full speed, and the sample's length.
export const RUN_UP_TICS = 35;
export const SAMPLE_MS = 3000;
// [rc4l] A step is uneven when it is this share off the median step; the unbuffered view must have more than UNEVEN_FLOOR of its steps so, and the buffered at most BUFFERED_AT_MOST of the unbuffered's share.
export const UNEVEN_BY = 0.3;
export const UNEVEN_FLOOR = 0.15;
export const BUFFERED_AT_MOST = 0.5;

// [rc4l] How evenly one player stepped across the samples ({ tic, actors }) taken a tic apart: the steps, the median, and how many were uneven.
export function stepUnevenness(samples, player) {
  const at = samples.map((s) => ({ tic: s.tic, a: (s.actors || []).find((x) => x.player === player) })).filter((s) => s.a);
  const steps = [];
  for (let i = 1; i < at.length; i++) {
    if (at[i].tic - at[i - 1].tic !== 1) continue;
    steps.push(Math.hypot(at[i].a.x - at[i - 1].a.x, at[i].a.y - at[i - 1].a.y));
  }
  const sorted = [...steps].sort((p, q) => p - q);
  const median = sorted.length ? sorted[Math.floor(sorted.length / 2)] : 0;
  const uneven = steps.filter((d) => Math.abs(d - median) > UNEVEN_BY * median).length;
  return { steps: steps.length, median, uneven, share: steps.length ? uneven / steps.length : 0 };
}

// [rc4l] What is wrong with the two views: too few steps or a still runner say nothing, the unbuffered view must be uneven enough to tell, and the buffered one at most half as uneven.
export function judgeEvenness(on, off) {
  for (const [name, r] of [["buffered", on], ["unbuffered", off]]) {
    if (r.steps < 40) return [`only ${r.steps} ${name} steps a tic apart were sampled`];
    if (!(r.median > 4)) return [`the runner barely moved ${name} (median step ${r.median.toFixed(2)})`];
  }
  if (!(off.share > UNEVEN_FLOOR)) return [`without the buffers only ${off.uneven} of ${off.steps} steps were uneven, so the check cannot tell`];
  if (on.share > BUFFERED_AT_MOST * off.share) return [`with the buffers ${on.uneven} of ${on.steps} steps were uneven, against ${off.uneven} of ${off.steps} without`];
  return [];
}

// [rc4l] Each tic's actors on the observer for ms, polled faster than a tic so none is missed.
async function sampleTics(c, ms) {
  const samples = [];
  const until = Date.now() + ms;
  let last = -1;
  while (Date.now() < until) {
    const r = await c.rpc("world.actors", { players: true }).catch(() => null);
    if (r && r.tic !== last) { samples.push({ tic: r.tic, actors: r.actors || [] }); last = r.tic; }
    await cap.sleep(4);
  }
  return samples;
}

export async function judgeNetplay(set, label, { config }) {
  const runner = set.clients.find((c) => c.role === "player");
  const observer = set.clients.find((c) => c.role === "observer");
  await cap.exec(runner.c, "+forward");
  await cap.waitTics(observer.c, RUN_UP_TICS);
  const on = stepUnevenness(await sampleTics(observer.c, SAMPLE_MS), runner.player);
  await cap.exec(observer.c, "cl_usemovebuffer 0");
  await cap.waitTics(observer.c, 5);
  const off = stepUnevenness(await sampleTics(observer.c, SAMPLE_MS), runner.player);
  await cap.exec(runner.c, "-forward").catch(() => {});
  const faults = judgeEvenness(on, off);
  const facts = `uneven steps ${on.uneven}/${on.steps} buffered, ${off.uneven}/${off.steps} not, at up to ${config.jitter.split(",").pop()} ms of jitter`;
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")} (${facts})` : `PASS ${label}: ${facts}`], failed: faults.length ? 1 : 0 };
}

// [rc4l] The observer's packets held up to JITTER ms each, in order, so they arrive in bursts; the runner's link is clean.
export const JITTER = process.env.NETMOVEBUFFER_JITTER || "40";
export const CONFIGS = [{ name: "bursty", players: 1, observers: 1, jitter: `0,${JITTER}`, clientCvars: { cl_run: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netmovebuffer", wad: buildNetMoveBufferPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

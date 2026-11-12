// [rc4l] The view's 180-degree turn and its centering, drawn between tics (zandronum@2d4749b0fa1d): exit 0 when frames inside one tic draw the turn's yaw and the centering's pitch at more than one angle, as they step a tic at a time without interpolation.
//
// usage:  node scenarios/viewturn.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] How long the turn and the centering are watched, both done in a third of a second.
export const WATCH_MS = 700;
// [rc4l] Tics of the turn or centering whose frames must show more than one angle, of the turn's 9.
export const MIN_SMOOTH_TICS = 3;
// [rc4l] Degrees the view looks down before it centers, past the two at which centering ends in one step.
export const LOOK_DOWN = 30;

export function buildViewTurnPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 512, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] Each frame's view.angle answer for ms, one per frame drawn.
export async function sampleView(c, ms) {
  const out = [];
  for (const until = Date.now() + ms; Date.now() < until; ) {
    const v = await c.rpc("view.angle").catch(() => null);
    const last = out[out.length - 1];
    if (v && !(last && last.gametic === v.gametic && last.frac === v.frac)) out.push(v);
  }
  return out;
}

// [rc4l] How many tics drew key at more than one angle across their frames.
export function smoothTics(samples, key) {
  const byTic = new Map();
  for (const s of samples) {
    if (!byTic.has(s.gametic)) byTic.set(s.gametic, new Set());
    byTic.get(s.gametic).add(s[key].toFixed(2));
  }
  return [...byTic.values()].filter((angles) => angles.size > 1).length;
}

// [rc4l] What is wrong with one motion's frames: it must have moved, and drawn between tics in enough of them.
export function judgeSmooth(what, samples, key) {
  if (samples.length < 2) return [`${what} drew ${samples.length} frames`];
  const moved = Math.abs(samples[samples.length - 1][key] - samples[0][key]);
  if (moved < 1) return [`${what} moved the view ${moved.toFixed(2)} degrees`];
  const n = smoothTics(samples, key);
  return n >= MIN_SMOOTH_TICS ? [] : [`${what} drew between tics in ${n} tics, not ${MIN_SMOOTH_TICS}`];
}

// [rc4l] Runs a console command at once, without waiting out the console's quiet as cap.exec does, which outlasts the motion.
const startMotion = (c, text) => c.rpc("console.exec", { text });

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  await startMotion(c, "turn180");
  const turn = await sampleView(c, WATCH_MS);
  const at = await c.rpc("player.camera");
  await c.rpc("player.setpos", { x: at.x, y: at.y, pitch: LOOK_DOWN });
  await cap.waitTics(c, 10);
  await startMotion(c, "centerview");
  const center = await sampleView(c, WATCH_MS);
  const faults = [...judgeSmooth("the turn", turn, "yaw"), ...judgeSmooth("the centering", center, "pitch")];
  const facts = `the turn in ${smoothTics(turn, "yaw")} tics, the centering in ${smoothTics(center, "pitch")}, of ${turn.length} and ${center.length} frames`;
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")} (${facts})` : `PASS ${label}: drawn between tics, ${facts}`, failed: faults.length ? 1 : 0 };
}

// [rc4l] Tics inline, since beside a game thread the bridge answers once a tic and sees one frame of each.
export const SIDES = [{ name: "vk", launchCvars: { fua_gamethread: 0 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "viewturn", wad: buildViewTurnPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

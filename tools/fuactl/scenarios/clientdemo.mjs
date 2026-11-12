// [rc4l] A clientside demo an observer recorded, played back: exit 0 when the running player it watched runs in the playback (zandronum@d04a70a98f3c) and the intermission's countdown holds while the playback is paused (zandronum@1d9962b2229f).
//
// usage:  node scenarios/clientdemo.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, PCD, SCRIPT, callSpecial } from "../src/acs.mjs";
import { readMenu } from "../src/ui.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addOffline } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] Where the observer's demo goes, beside no other run's.
export const DEMO = path.join(os.tmpdir(), "fuactl-clientdemo", `zxdemo-${process.pid}`).split(path.sep).join("/");
// [rc4l] Tics the runner runs while the observer records, and tics of the intermission recorded after.
export const RUN_TICS = 105, INTERMISSION_TICS = 140;
// [rc4l] How long the paused playback is watched for its countdown to move.
export const PAUSED_MS = 2500;
export const MAPINFO = `map MAP01 "Run"\n{\n  next = "MAP02"\n}\n\nmap MAP02 "After"\n{\n}\n`;

// [rc4l] A corridor long enough to run up for the whole recording, the runner's start ahead of the observer's, and a closed script that ends the map.
export function buildClientDemoPk3() {
  const corridor = () => {
    const m = new UdmfMap();
    m.room(0, -256, 256, 8192, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    m.thing({ x: 96, y: 64, angle: 90, type: 1 });
    m.thing({ x: 160, y: -128, angle: 90, type: 2 });
    return m;
  };
  const exit = [{ code: [...callSpecial(SPECIAL.Exit_Normal, [0]), PCD.TERMINATE], type: SCRIPT.CLOSED, number: 1 }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", corridor(), { behavior: acsScripts(exit, []) })) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", corridor())) },
    { name: "mapinfo.txt", data: MAPINFO },
  ]);
}

// [rc4l] How far one player went across samples a tic or more apart, and their median step a tic apart.
export function travel(samples, player) {
  const at = samples.map((s) => ({ tic: s.tic, a: (s.actors || []).find((x) => x.player === player) })).filter((s) => s.a);
  const steps = [];
  let total = 0;
  for (let i = 1; i < at.length; i++) {
    const d = Math.hypot(at[i].a.x - at[i - 1].a.x, at[i].a.y - at[i - 1].a.y);
    total += d;
    if (at[i].tic - at[i - 1].tic === 1) steps.push(d);
  }
  const sorted = [...steps].sort((p, q) => p - q);
  return { samples: at.length, total, median: sorted.length ? sorted[Math.floor(sorted.length / 2)] : 0 };
}

// [rc4l] What is wrong with the playback's level: the runner must be there and go the length of a run.
export function judgeTravel(r) {
  if (r.samples < 20) return [`the playback held the runner in only ${r.samples} samples`];
  if (!(r.total > 100)) return [`the runner went ${r.total.toFixed(1)} units in the playback`];
  return [];
}

// [rc4l] The seconds the intermission's board says are left, or null when it says none.
export function secondsLeft(lines) {
  const line = (lines || []).find((l) => /Entering .* in \d+ seconds?/.test(l.text));
  return line ? Number(/in (\d+) second/.exec(line.text)[1]) : null;
}

// [rc4l] What is wrong with the paused countdown: it must be shown, and read the same before and after.
export function judgeCountdown(before, after) {
  if (before === null || after === null) return ["the playback's intermission showed no countdown"];
  return before === after ? [] : [`the paused playback's countdown went from ${before} to ${after} seconds`];
}

// [rc4l] Each tic's player pawns on c until the level gives way to the intermission, or ms pass.
export async function sampleLevel(c, ms) {
  const samples = [];
  let last = -1;
  for (const until = Date.now() + ms; Date.now() < until; await cap.sleep(4)) {
    const t = await c.rpc("sim.tic").catch(() => ({}));
    if (samples.length && t.gamestate === cap.GS_INTERMISSION) break;
    const r = await c.rpc("world.actors", { players: true }).catch(() => null);
    if (r && r.tic !== last && (r.actors || []).length) { samples.push({ tic: r.tic, actors: r.actors }); last = r.tic; }
  }
  return samples;
}

// [rc4l] Records while the runner runs and the server ends the map, quits the recorder in the intermission to write the demo, and plays it back on an offline instance, since a disconnect sends a client to its Continue record.
export async function recordAndPlay(set) {
  const runner = set.clients.find((c) => c.role === "player");
  const recorder = set.clients.find((c) => c.role === "observer");
  await cap.exec(runner.c, "+forward");
  await cap.waitTics(set.server.c, RUN_TICS);
  await cap.exec(runner.c, "-forward").catch(() => {});
  await cap.exec(set.server.c, "puke 1");
  await cap.waitTics(set.server.c, INTERMISSION_TICS);
  await cap.exec(recorder.c, "quit").catch(() => {});
  recorder.c.close();
  recorder.c = null;
  const file = `${DEMO}.cld`;
  for (const until = Date.now() + 10000; !fs.existsSync(file) && Date.now() < until; ) await cap.sleep(100);
  if (!fs.existsSync(file)) return { runner, viewer: null, faults: [`the recorder wrote no demo at ${file}`] };
  const viewer = await addOffline(set);
  await cap.exec(viewer.c, `playdemo ${DEMO}`);
  await cap.waitInLevel(viewer.c);
  return { runner, viewer, faults: [] };
}

// [rc4l] Pauses the playback once its intermission shows the countdown, and reads it again PAUSED_MS later.
async function pausedCountdown(c) {
  let before = null;
  for (const until = Date.now() + 20000; before === null && Date.now() < until; await cap.sleep(200)) before = secondsLeft((await readMenu(c)).lines);
  if (before === null) return { before, after: null };
  await cap.exec(c, "demo_pause");
  before = secondsLeft((await readMenu(c)).lines);
  await cap.sleep(PAUSED_MS);
  return { before, after: secondsLeft((await readMenu(c)).lines) };
}

export async function judgeNetplay(set, label) {
  fs.mkdirSync(path.dirname(DEMO), { recursive: true });
  const { runner, viewer, faults } = await recordAndPlay(set);
  let facts = "";
  if (!faults.length) {
    const r = travel(await sampleLevel(viewer.c, 30000), runner.player);
    const { before, after } = await pausedCountdown(viewer.c);
    faults.push(...judgeTravel(r), ...judgeCountdown(before, after));
    facts = `the runner went ${r.total.toFixed(0)} units, the paused countdown read ${before} then ${after}`;
  }
  try { fs.rmSync(`${DEMO}.cld`, { force: true }); } catch { /* ignore */ }
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")} (${facts})` : `PASS ${label}: ${facts}`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "playback", players: 1, observers: 1, clientCvars: { cl_run: 1 }, clientExtraArgs: [[], ["-record", DEMO]] }];

runScenarioIfMain(import.meta.url, () => ({ name: "clientdemo", wad: buildClientDemoPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

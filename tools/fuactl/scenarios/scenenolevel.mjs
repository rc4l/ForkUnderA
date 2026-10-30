// [Doruk-Aksoy] The commands that draw the backend's scene themselves, run after endgame: each refuses and the engine keeps running, where they drew the freed level through the game copy and crashed, and a shot in the next level still writes its picture.
//
// usage:  node scenarios/scenenolevel.mjs [port]
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { captureVulkan } from "../src/shot.mjs";

const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 200 };
const WALL = { side: { texturemiddle: "STARTAN2" } };

// [Doruk-Aksoy] Rooms a side, enough that the freed sector array is a block the heap hands back to the system, so the stale read faults every time as it did on a large map.
export const GRID = 64;

// [Doruk-Aksoy] What each command prints when it refuses outside a level.
export const REFUSAL = "FAILED: not in a level";

// [Doruk-Aksoy] A grid of closed rooms, the player in the first.
export function buildSceneNoLevelWad(grid = GRID) {
  const m = new UdmfMap();
  for (let x = 0; x < grid; x++) for (let y = 0; y < grid; y++) m.room(x * 128, y * 128, x * 128 + 96, y * 128 + 96, ROOM, () => WALL);
  m.thing({ x: 48, y: 48, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] Every command that draws the scene without a frame asking, the shot writing to file.
export function sceneCommands(file) {
  return [`fua_diligent_shot "${file}"`, "fua_diligent_bench 5", "fua_diligent_scale 2"];
}

// [Doruk-Aksoy] The verdict: alive and uncrashed, each command refused with nothing written, then a shot in the next level written.
export function judgeSceneNoLevel(label, { crashed, alive, outs, wroteOutside, wroteInLevel }) {
  if (crashed || !alive) {
    const last = outs.length ? outs[outs.length - 1].command : "endgame";
    return { line: `FAIL ${label}: the engine ${crashed ? "crashed" : "stopped answering"} at ${last} outside a level`, failed: 1 };
  }
  const drew = outs.find((o) => !o.out.includes(REFUSAL));
  if (drew) return { line: `FAIL ${label}: ${drew.command} outside a level printed "${drew.out.trim()}" rather than refusing`, failed: 1 };
  if (wroteOutside) return { line: `FAIL ${label}: the shot outside a level wrote a picture`, failed: 1 };
  if (!wroteInLevel) return { line: `FAIL ${label}: a shot in the next level wrote no picture`, failed: 1 };
  return { line: `PASS ${label}: ${outs.length} scene commands refused outside a level and a shot in the next level wrote its picture`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  const outside = path.join(os.tmpdir(), "fua-scenenolevel-outside.png");
  const inLevel = path.join(os.tmpdir(), "fua-scenenolevel-level.png");
  for (const f of [outside, inLevel]) fs.rmSync(f, { force: true });
  await c.rpc("console.exec", { text: "endgame" });
  // [Doruk-Aksoy] Real time, for the full console's frames after the level was freed.
  await cap.sleep(2500);
  const outs = [];
  for (const command of sceneCommands(outside)) {
    const out = await cap.exec(c, command, { quietMs: 500, maxMs: 15000 }).catch(() => null);
    outs.push({ command, out: out ?? "" });
    if (out === null) break;
  }
  const alive = await c.rpc("sim.tic").then(() => true, () => false);
  let wroteInLevel = false;
  if (alive) {
    await c.rpc("console.exec", { text: "map MAP01" });
    await cap.waitInLevel(c);
    await cap.waitTics(c, 35);
    wroteInLevel = (await captureVulkan(c, inLevel)) !== null;
  }
  return judgeSceneNoLevel(label, { crashed: fs.existsSync(`${inst.log}.crash`), alive, outs, wroteOutside: fs.existsSync(outside), wroteInLevel });
}

runScenarioIfMain(import.meta.url, () => ({ name: "scenenolevel", wad: buildSceneNoLevelWad(), maps: ["MAP01"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);

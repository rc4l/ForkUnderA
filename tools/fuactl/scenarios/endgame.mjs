// [rc4l] A level with a 3D floor and a polyobject ended from the console, as the menu's End Game and a client's disconnect end it: the engine still running at the full console a moment later, and a new game after it playing.
//
// usage:  node scenarios/endgame.mjs [port]
import fs from "node:fs";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { ENGINE_THING, SPECIAL } from "../src/doomnums.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 200 };
const WALL = { side: { texturemiddle: "STARTAN2" } };
const BRIDGE_TAG = 1;

// [rc4l] One room holding a 3D floor's slab and a polyobject, each a list the render copy keeps by index into the level.
export function buildEndgameWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { ...ROOM, id: BRIDGE_TAG }, () => WALL);
  m.room(2048, 0, 2112, 64, { ...ROOM, heightfloor: 32, heightceiling: 64, texturefloor: "FLAT14", textureceiling: "FLAT14" }, (edge) => (edge === "west"
    ? { ...WALL, line: { special: SPECIAL.Sector_Set3DFloor, arg0: BRIDGE_TAG, arg1: 1, arg2: 0, arg3: 255 } } : WALL));
  // [rc4l] The polyobject's template, a 128 by 8 box walked counter-clockwise so every face points out, in a room of its own.
  const tpl = m.room(4096, 0, 4352, 256, ROOM, () => WALL);
  const face = { texturemiddle: "BIGDOOR1" };
  m.wall(4160, 124, 4288, 124, tpl, face, { special: SPECIAL.Polyobj_StartLine, arg0: 1 });
  m.wall(4288, 124, 4288, 132, tpl, face);
  m.wall(4288, 132, 4160, 132, tpl, face);
  m.wall(4160, 132, 4160, 124, tpl, face);
  m.thing({ x: 4224, y: 128, angle: 1, type: ENGINE_THING.PolyAnchor });
  m.thing({ x: 768, y: 512, angle: 1, type: ENGINE_THING.PolySpawn });
  m.thing({ x: 128, y: 512, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] The verdict: alive and uncrashed at the full console, then in a level again with its tics running.
export function judgeEndgame(label, { crashed, alive, playing }) {
  if (crashed || !alive) return { line: `FAIL ${label}: the engine ${crashed ? "crashed" : "stopped answering"} at the full console after endgame`, failed: 1 };
  if (!playing) return { line: `FAIL ${label}: a new game after endgame did not play`, failed: 1 };
  return { line: `PASS ${label}: endgame left the engine at the full console and a new game played`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await c.rpc("console.exec", { text: "endgame" });
  // [rc4l] Real time, since the full console's tics are what touched the freed level.
  await cap.sleep(2500);
  const answers = async () => c.rpc("sim.tic").then(() => true, () => false);
  const alive = await answers();
  let playing = false;
  if (alive) {
    await c.rpc("console.exec", { text: "map MAP01" });
    await cap.waitInLevel(c);
    const start = (await c.rpc("sim.tic")).gametic;
    await cap.waitTics(c, 35);
    playing = (await c.rpc("sim.tic")).gametic > start;
  }
  return judgeEndgame(label, { crashed: fs.existsSync(`${inst.log}.crash`), alive, playing });
}

runScenarioIfMain(import.meta.url, () => ({ name: "endgame", wad: buildEndgameWad(), maps: ["MAP01"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);

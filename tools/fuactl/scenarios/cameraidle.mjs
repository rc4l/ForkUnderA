// [rc4l] A map with a camera texture left running in a window without focus, as alt-tabbing away from a netgame or with i_pauseinbackground off leaves it: the engine still running after its frames have gone unpresented for a while, each one finished so its camera views' memory comes back (queue #49).
//
// usage:  node scenarios/cameraidle.mjs [port]
import fs from "node:fs";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import * as cap from "../src/capture.mjs";
import { activateEngineWindows } from "../src/window.mjs";

const CAMERA_TID = 5;

// [rc4l] One room whose east wall shows what a camera across it sees.
export function buildCameraIdleWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 },
    (edge) => ({ side: { texturemiddle: edge === "east" ? "IDLECAM" : "STARTAN2" } }));
  m.thing({ x: 128, y: 512, angle: 0, type: 1 });
  m.thing({ x: 512, y: 512, height: 64, angle: 0, type: THING.MapSpot, id: CAMERA_TID });
  const behavior = acs0([PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE, PCD.TERMINATE], ["IDLECAM"]);
  return wad([{ name: "ANIMDEFS", data: "cameratexture IDLECAM 256 128\n" }, ...mapLumps("MAP01", m, { behavior })]);
}

// [rc4l] The verdict: told it was switched away from, alive and uncrashed after the wait, its tics still running.
export function judgeCameraIdle(label, { told, crashed, alive, ticked, seconds }) {
  if (!told) return { line: `FAIL ${label}: no window to switch away from`, failed: 1 };
  if (crashed || !alive) return { line: `FAIL ${label}: the engine ${crashed ? "crashed" : "stopped answering"} within ${seconds} s of unpresented frames`, failed: 1 };
  if (!ticked) return { line: `FAIL ${label}: the game stopped running while unfocused`, failed: 1 };
  return { line: `PASS ${label}: ${seconds} s of unpresented frames with a camera texture, still running`, failed: 0 };
}

const SECONDS = 15;

export async function judgeLaunch(c, inst, label) {
  // [rc4l] A window without focus is not presented to unless the first is on, and a game kept running there (the second off, or any netgame client) still draws its frames.
  await cap.exec(c, "fua_render_in_background 0");
  await cap.exec(c, "i_pauseinbackground 0");
  // [rc4l] As alt-tab does, with nothing on the desktop moved.
  const told = activateEngineWindows(inst.pid, false);
  const start = (await c.rpc("sim.tic")).gametic;
  await cap.sleep(SECONDS * 1000);
  const now = await c.rpc("sim.tic").catch(() => null);
  if (now !== null) activateEngineWindows(inst.pid, true);
  return judgeCameraIdle(label, { told, crashed: fs.existsSync(`${inst.log}.crash`), alive: now !== null, ticked: now !== null && now.gametic > start, seconds: SECONDS });
}

runScenarioIfMain(import.meta.url, () => ({ name: "cameraidle", wad: buildCameraIdleWad(), maps: ["MAP01"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);

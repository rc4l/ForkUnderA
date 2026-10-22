// [rc4l] The pointer over the menus at a render scale: exit 0 when a window pixel maps to the render pixel under it on GL and on the backend alone, which left it unscaled so every hover at vid_scalefactor 0.5 landed twice as far in.
//
// usage:  node scenarios/pointerscale.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch, judgeWindowPoint } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

export function buildPointerScaleWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const SIDES = [{ name: "gl", gl: true }, { name: "vk", noGl: true }];

// [rc4l] A point near the far corner of the 1280x720 window, which half scale puts at half its coordinates in a 640x360 frame.
const FROM = [1000, 600];
const WANT = { from: FROM, x: 500, y: 300, width: 640, height: 360 };

export async function judgePointer(c, inst, label) {
  await cap.exec(c, "vid_scalefactor 0.5");
  await cap.waitTics(c, 20);
  return judgeWindowPoint(label, await c.rpc("input.windowpoint", { x: FROM[0], y: FROM[1] }), WANT);
}

runScenarioIfMain(import.meta.url, () => ({ name: "pointerscale", wad: buildPointerScaleWad(), maps: ["MAP01"], sides: SIDES,
  judgeLaunch: judgePointer }), compareEachLaunch);

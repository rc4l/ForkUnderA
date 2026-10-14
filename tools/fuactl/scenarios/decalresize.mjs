// [rc4l] A floor mark that must survive the video settings changing under it, Vulkan captured after each step: exit 0 when a rocket's scorch appears and looks the same after a window resize (which rebuilds the scene's targets, as fullscreen and back does), a render scale and an aspect change.
//
// usage:  node scenarios/decalresize.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";

// [rc4l] One plain lit hall, so the only thing on its floor is the scorch.
export function buildDecalResizeWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Where the rocket is fired and each setting changed, looking down at the floor ahead.
const FIRE = { x: 512, y: 300, z: 0, angle: 90, pitch: 60 };
// [rc4l] The marks seen from elsewhere, since a pass still sampling the depth from before a change matches only the view it was frozen in.
const SIDE = { x: 440, y: 250, z: 0, angle: 45, pitch: 25 };
export const VANTAGES = [{ name: "side", at: SIDE }];

// [rc4l] Each change is made from FIRE and comes back, so the frame is the marked one's size again; a size change rebuilds the scene's targets, which the decal pass samples.
const changed = (a, b) => [FIRE, 500, a, 1500, b, 1500, SIDE, 500];
export const STEPS = [
  // [rc4l] A locked launch is already in god mode, which keeps the blast from killing the player; god is a toggle, so asking again would turn it off.
  { name: "clean", commands: ["r_drawplayersprites 0", "crosshair 0", "give RocketLauncher", "slot 5", 1000] },
  { name: "marked", commands: [FIRE, "+attack", 150, "-attack", 3000, SIDE], waitMs: 500 },
  { name: "resized", commands: changed("vid_setsize 1024 600", "vid_setsize 1280 720") },
  { name: "scaled", commands: changed("vid_scalefactor 0.5", "vid_scalefactor 1") },
  { name: "aspect", commands: changed("vid_aspect 3", "vid_aspect 0") },
];

const AT_MARKS = [0.41, 0.39, 0.57, 0.51];
export const BOXES = { MAP01: {
  made: { box: AT_MARKS, pair: ["clean", "marked"], floor: 2 },
  resized: { box: AT_MARKS, pair: ["marked", "resized"], ceiling: 0.5 },
  scaled: { box: AT_MARKS, pair: ["marked", "scaled"], ceiling: 0.5 },
  aspect: { box: AT_MARKS, pair: ["marked", "aspect"], ceiling: 0.5 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "decalresize", wad: buildDecalResizeWad(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);

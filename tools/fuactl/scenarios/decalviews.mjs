// [rc4l] features/decals' marks as UZDoom shows them, Vulkan captured after each step: a rocket's scorch is drawn in a mirror, a camera texture, a stacked sector's view and a reflecting floor, a wall's scorch stays when its texture comes to refuse decals (refusal is judged when a mark is made), and the floor's scorch is held off its floor once that refuses them (the per-pixel bit).
//
// usage:  node scenarios/decalviews.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";

const HALL_TAG = 1, WEST_ID = 2, CAMERA_TID = 5;
// [rc4l] The same pictures as STARTAN2 and FLOOR4_8, refusing decals, so changing to them shows nothing but what the marks do.
const TEXTURES = `WallTexture "NODECW", 128, 128
{
  NoDecals
  Patch "STARTAN2", 0, 0
}
Flat "NODECF", 64, 64
{
  NoDecals
  Patch "FLOOR4_8", 0, 0
}
`;
const ANIMDEFS = "cameratexture DVCAM 256 128\n";

// [rc4l] The hall stands clear of the stacked rooms the helper puts at the origin.
const X0 = 2048, REFLECT_X = 6144, POOL_TAG = 3;
const ROOM = { heightfloor: 0, textureceiling: "CEIL3_5", texturefloor: "FLOOR4_8", lightlevel: 255 };

// [rc4l] One lit hall (a mirror on the north wall, the camera's picture on the south wall, the west wall the script retextures, the camera looking east along the floor), stacked rooms whose upper floor looks into the lower room, and a room whose floor reflects its ceiling.
export function buildDecalViewsWad() {
  const m = new UdmfMap();
  const STARTAN = { texturemiddle: "STARTAN2" };
  m.stackedRooms({ dx: 4096 });
  m.room(X0, 0, X0 + 1024, 1024, { ...ROOM, heightceiling: 256, id: HALL_TAG },
    (edge) => ({ north: { side: STARTAN, line: { special: SPECIAL.Line_Mirror } }, south: { side: { texturemiddle: "DVCAM" } },
      west: { side: STARTAN, line: { id: WEST_ID } }, east: { side: STARTAN } })[edge]);
  m.room(REFLECT_X, 0, REFLECT_X + 512, 512, { ...ROOM, heightceiling: 128, id: POOL_TAG }, () => ({ side: STARTAN }));
  m.thing({ x: X0 + 512, y: 512, angle: 90, type: 1 });
  m.thing({ x: X0 + 200, y: 200, height: 64, angle: 0, type: THING.MapSpot, id: CAMERA_TID });
  const open = [PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE,
    ...callSpecial(SPECIAL.Sector_SetPlaneReflection, [POOL_TAG, 160, 0]), PCD.TERMINATE];
  // [rc4l] Script 2: the west wall's middle and the hall's floor changed to the copies refusing decals.
  const retexture = [PCD.PUSHNUMBER, WEST_ID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 1, PCD.SETLINETEXTURE,
    PCD.CHANGEFLOORDIRECT, HALL_TAG, 2, PCD.TERMINATE];
  const behavior = acsScripts([{ code: open }, { code: retexture, open: false, number: 2 }], ["DVCAM", "NODECW", "NODECF"]);
  return wad([{ name: "TEXTURES", data: TEXTURES }, { name: "ANIMDEFS", data: ANIMDEFS }, ...mapLumps("MAP01", m, { behavior })]);
}

const hall = (x, y, angle, pitch) => ({ x: X0 + x, y, z: 0, angle, pitch });
// [rc4l] Each scorch's rocket, fired a few units short of where it lands: before the mirror, ahead of the camera, on the west wall, on the lower stacked room's floor, and on the reflecting room's ceiling.
const FIRE_MIRROR = hall(700, 900, 90, 60);
const FIRE_CAMERA = hall(400, 200, 0, 60);
const FIRE_WALL = hall(160, 600, 180, 0);
const FIRE_STACK = { x: 256, y: 236, z: 0, angle: 90, pitch: 60 };
const FIRE_REFLECT = { x: REFLECT_X + 256, y: 236, z: 0, angle: 90, pitch: -60 };
// [rc4l] The views: the mirror with its scorch behind the viewer, so only the reflection shows it; the camera's picture with its scorch behind; the west wall; the camera's scorch from above; the lower room's scorch through the upper room's floor; and the ceiling's scorch in the floor below it.
const MIRROR = hall(600, 980, 90, 0);
const CAMERA = hall(512, 100, 270, 0);
const WALL = hall(300, 600, 180, 0);
const FLOOR = hall(420, 100, 90, 45);
const STACK = { x: 4096 + 256, y: 160, z: 128, angle: 90, pitch: 60 };
const REFLECT = { x: REFLECT_X + 256, y: 100, z: 0, angle: 90, pitch: 45 };
export const VANTAGES = [{ name: "v", at: MIRROR }];

const fire = (at) => [at, "+attack", 150, "-attack", 2500];
const views = (prefix) => [["mirror", MIRROR], ["camera", CAMERA], ["wall", WALL], ["floor", FLOOR], ["stack", STACK], ["reflect", REFLECT]]
  .map(([name, at]) => ({ name: `${prefix}${name}`, commands: [at], waitMs: 400 }));
export const STEPS = [
  // [rc4l] A locked launch is already in god mode, which keeps the blasts from killing the player; no notices, since a capture's own line lands in the next.
  { name: "setup", commands: ["con_notifylines 0", "r_drawplayersprites 0", "crosshair 0", "give RocketLauncher", "give ammo", "slot 5", 1000] },
  ...views("clean"),
  { name: "fired", commands: [...fire(FIRE_MIRROR), ...fire(FIRE_CAMERA), ...fire(FIRE_WALL), ...fire(FIRE_STACK), ...fire(FIRE_REFLECT)] },
  ...views("marked"),
  { name: "retextured", commands: ["puke 2", 300] },
  ...views("retex"),
];

// [rc4l] Each box around its scorch: the reflection right of the player's own, the camera tile's at the frame's left edge, the wall's, the floor's, the lower room's seen through the upper floor, and the ceiling's in the floor.
const AT_REFLECTION = [0.7, 0.67, 0.85, 0.77], AT_CAMERA_TILE = [0.0, 0.66, 0.06, 0.76], AT_WALL = [0.46, 0.46, 0.53, 0.58], AT_FLOOR = [0.44, 0.14, 0.59, 0.27];
const AT_STACKED = [0.46, 0.43, 0.54, 0.54], AT_REFLECTED_CEILING = [0.47, 0.4, 0.54, 0.51];
export const BOXES = { MAP01: {
  mirror: { box: AT_REFLECTION, pair: ["cleanmirror", "markedmirror"], floor: 2 },
  camera: { box: AT_CAMERA_TILE, pair: ["cleancamera", "markedcamera"], floor: 2 },
  walldecal: { box: AT_WALL, pair: ["cleanwall", "markedwall"], floor: 2 },
  wallstays: { box: AT_WALL, pair: ["markedwall", "retexwall"], ceiling: 0.5 },
  floormark: { box: AT_FLOOR, pair: ["cleanfloor", "markedfloor"], floor: 2 },
  floorhidden: { box: AT_FLOOR, pair: ["cleanfloor", "retexfloor"], ceiling: 0.5 },
  mirrorhidden: { box: AT_REFLECTION, pair: ["cleanmirror", "retexmirror"], ceiling: 0.5 },
  stack: { box: AT_STACKED, pair: ["cleanstack", "markedstack"], floor: 2 },
  reflect: { box: AT_REFLECTED_CEILING, pair: ["cleanreflect", "markedreflect"], floor: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "decalviews", wad: buildDecalViewsWad(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);

// [rc4l] A sky other than the level's (a Sky2 sector's sky2) seen through each kind of view, GL against Vulkan: exit 0 when Vulkan shows it where GL does in a stacked view, a camera texture, a line mirror and a reflective floor, not the level's sky.
//
// usage:  node scenarios/skyviews.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

const SKY2_SECTOR = 200, MIRROR_TAG = 7, CAMERA_TID = 5;
const ANIMDEFS = "cameratexture FUACAM 256 128 fit 128 64\n";

// [rc4l] Every map's sky1 is SKY1 and its sky2 SKY3, which looks nothing like it.
const MAPINFO = ["MAP01", "MAP02", "MAP03", "MAP04"].map((m) => `map ${m} "Sky views"\n{\n\tsky1 = "SKY1", 0\n\tsky2 = "SKY3", 0\n}\n`).join("");

const sky2 = { texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 255, special: SKY2_SECTOR };

// [rc4l] MAP01: a room whose ceiling is stacked onto an upper room open to the sky2.
function stacked() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 160 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.room(4096, 0, 4608, 512, { ...sky2, heightfloor: 128, heightceiling: 640 }, () => ({ side: { texturemiddle: "BRICK1" } }));
  m.thing({ x: 64, y: 64, angle: 0, type: THING.LowerStackLookOnly, id: 7 });
  m.thing({ x: 4160, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: 7 });
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return mapLumps("MAP01", m);
}

// [rc4l] MAP02: a screen on the north wall showing a camera in a low sky2 room, its sky over the far wall.
function camera() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => ({ side: { texturemiddle: edge === "north" ? "FUACAM" : "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  m.room(1792, 0, 2304, 512, { ...sky2, heightfloor: 0, heightceiling: 96 }, () => ({ side: { texturemiddle: "COMPBLUE" } }));
  m.thing({ x: 1856, y: 256, height: 32, angle: 0, type: 9001, id: CAMERA_TID });
  return mapLumps("MAP02", m, { behavior: acs0([PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90,
    PCD.SETCAMERATOTEXTURE, PCD.TERMINATE], ["FUACAM"]) });
}

// [rc4l] MAP03: a mirror on the north wall, and behind the player a sky2 room as tall as the player's that only the mirror shows.
function mirror() {
  const m = new UdmfMap();
  const a = m.sector({ heightfloor: 0, heightceiling: 384, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 });
  const c = m.sector({ ...sky2, heightfloor: 0, heightceiling: 384 });
  const wall = { texturemiddle: "STARTAN2" };
  m.wall(0, 0, 0, 512, a, wall);
  m.wall(0, 512, 512, 512, a, wall, { special: SPECIAL.Line_Mirror });
  m.wall(512, 512, 512, 0, a, wall);
  m.opening(512, 0, 0, 0, a, c, {}, { texturetop: "BRICK1" });
  m.wall(0, -512, 0, 0, c, { texturemiddle: "BRICK1" });
  m.wall(512, -512, 0, -512, c, { texturemiddle: "BRICK1" });
  m.wall(512, 0, 512, -512, c, { texturemiddle: "BRICK1" });
  m.thing({ x: 256, y: 384, angle: 90, type: 1 });
  return mapLumps("MAP03", m);
}

// [rc4l] MAP04: a reflective floor under a sky2 ceiling, the sky in its reflection.
function floor() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { ...sky2, heightfloor: 0, heightceiling: 256, texturefloor: "FLAT1", id: MIRROR_TAG },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return mapLumps("MAP04", m, { behavior: acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [MIRROR_TAG, 160, 0]), PCD.TERMINATE]) });
}

export function buildSkyViewsWad() {
  return wad([{ name: "MAPINFO", data: MAPINFO }, { name: "ANIMDEFS", data: ANIMDEFS },
    ...stacked(), ...camera(), ...mirror(), ...floor()]);
}

// [rc4l] Spots every map has floor at: up for the stacked view, at the screen, up into the mirror, down at the floor.
export const VANTAGES = [
  { name: "up", at: { x: 256, y: 256, z: 0, angle: 90, pitch: -50 } },
  { name: "ahead", at: { x: 256, y: 384, z: 0, angle: 90, pitch: 0 } },
  { name: "mirror", at: { x: 256, y: 384, z: 0, angle: 90, pitch: -20 } },
  { name: "down", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 35 } },
];

// [rc4l] Where each view shows the sky2: overhead, the band of it in the screen, the reflection in the mirror, the floor.
export const BOXES = {
  MAP01: { stack: { vantage: "up", box: [0.3, 0.0, 0.7, 0.32], ceiling: 3 } },
  MAP02: { screen: { vantage: "ahead", box: [0.2, 0.36, 0.8, 0.46], ceiling: 3 } },
  MAP03: { mirror: { vantage: "mirror", box: [0.36, 0.39, 0.62, 0.55], ceiling: 3 } },
  MAP04: { floor: { vantage: "down", box: [0.15, 0.55, 0.85, 0.98], ceiling: 3 } },
};

runScenarioIfMain(import.meta.url, () => ({ name: "skyviews", wad: buildSkyViewsWad(), maps: ["MAP01", "MAP02", "MAP03", "MAP04"],
  vantages: VANTAGES, boxes: BOXES }));

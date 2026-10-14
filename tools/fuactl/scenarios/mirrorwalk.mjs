// [rc4l] A model in a room whose sky and floor patch are GL portals, against the same room with neither: exit 0 when the portals' walks leave the model in the frame.
//
// usage:  node scenarios/mirrorwalk.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { kvxBox, voxelLumps } from "../src/kvx.mjs";

const PATCH_TAG = 3;

// [rc4l] The room, a reflective patch ahead and to the right, and a lamp whose voxel model GL draws before its portals; `reflecting` adds the patch's reflection and the skybox sky.
function room(reflecting) {
  const m = new UdmfMap();
  // [rc4l] A skybox sky is a portal too, and its walk sees only the skybox room, so whatever it clears it never draws back.
  const hall = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8",
    textureceiling: reflecting ? "F_SKY1" : "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(600, 400, 700, 500, { heightfloor: 0, heightceiling: 192, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 192, id: PATCH_TAG }, hall);
  m.thing({ x: 512, y: 200, angle: 90, type: 1 });
  m.thing({ x: 330, y: 600, angle: 0, type: THING.Column });
  if (reflecting) m.skyboxRoom(4000, 4000);
  const behavior = reflecting ? acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [PATCH_TAG, 200, 0]), PCD.TERMINATE]) : null;
  return mapLumps(reflecting ? "MAP01" : "MAP02", m, { behavior });
}

export function buildMirrorWalkWad() {
  return wad([...voxelLumps("COLU", kvxBox(16, 16, 48)), ...room(true), ...room(false)]);
}

export const VANTAGES = [{ name: "patch", at: { x: 512, y: 200, z: 0, angle: 90, pitch: 10 } }];

// [rc4l] The lamp's model, which GL's mirror walk used to wipe from the frame after the player's scene had drawn it.
export const BOXES = { MAP01: { lamp: { box: [0.2, 0.3, 0.42, 0.5], ceiling: 0.05 } } };

// [rc4l] The room with no reflection is what the lamp must look like, so the second side loads MAP02.
export const SIDES = [{ name: "reflecting" }, { name: "plain", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorwalk", wad: buildMirrorWalkWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));

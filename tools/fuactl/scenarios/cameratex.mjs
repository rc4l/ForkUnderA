// [rc4l] A one-sided wall showing a camera texture, GL against Vulkan: exit 0 when Vulkan shows the camera's picture as GL does, not one row of it stretched down the wall.
//
// usage:  node scenarios/cameratex.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI20's TV (sidedef 1023, TCAMTEX1): GL keeps a camera texture upside down and shifts its v back into 0..1, which the map bake did not, so the screen sampled one clamped row; twice as wide as tall, as a TV is, so a field of view taken the wrong way round shows, and fitted smaller than its pixels, so a picture drawn at the fitted size shows coarse.
const ANIMDEFS = "cameratexture FUACAM 256 128 fit 128 64\n";
const CAMERA_TID = 5;

export function buildCameraTexWad() {
  const m = new UdmfMap();
  // [rc4l] The player's room, its north wall the screen.
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => ({ side: { texturemiddle: edge === "north" ? "FUACAM" : "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  // [rc4l] What the camera sees: a bright room, red floor, blue walls, grey ceiling, so every row of its picture differs from the top one.
  m.room(1792, 0, 2304, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "COMPBLUE" } }));
  m.thing({ x: 1856, y: 256, height: 120, angle: 0, type: 9001, id: CAMERA_TID });
  // [rc4l] SetCameraToTexture(CAMERA_TID, "FUACAM", 90) from an OPEN script.
  const behavior = acs0([PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90,
    PCD.SETCAMERATOTEXTURE, PCD.TERMINATE], ["FUACAM"]);
  return wad([{ name: "ANIMDEFS", data: ANIMDEFS }, ...mapLumps("MAP01", m, { behavior })]);
}

// [rc4l] Facing the screen from the south end of the room.
export const VANTAGES = [{ name: "screen", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The screen alone, where a picture on its head or framed with the wrong field of view shows.
export const BOXES = { MAP01: { screen: { box: [0.26, 0.33, 0.74, 0.57], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "cameratex", wad: buildCameraTexWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

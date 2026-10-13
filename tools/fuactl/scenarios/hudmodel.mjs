// [rc4l] The pistol as a HUD model (a voxel box through MODELDEF), GL against Vulkan beside GL, standalone and with no GL: exit 0 when each draws the box over the world where GL does, rather than dropping the weapon because only GL's own HUD model path drew it.
//
// usage:  node scenarios/hudmodel.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A plain room and a MODELDEF that puts a 16-unit voxel box, skinned plain green, on every frame of the pistol, 64 units ahead of the eye and below it.
export function buildHudModelPk3({ offset = "0 -64 -24", scale = "1.0 1.0 1.0" } = {}) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  const skin = Buffer.alloc(16 * 16 * 3);
  for (let i = 0; i < 16 * 16; i++) { skin[i * 3] = 32; skin[i * 3 + 1] = 200; skin[i * 3 + 2] = 64; }
  const frames = ["A", "B", "C", "D", "E"].map((f) => `  FrameIndex PISG ${f} 0 0`).join("\n");
  const modeldef = `Model Pistol\n{\n  Path "models/"\n  Model 0 "hudbox.kvx"\n  Skin 0 "hudskin.png"\n  Scale ${scale}\n  Offset ${offset}\n${frames}\n}\n`;
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "modeldef.txt", data: modeldef },
    { name: "models/hudbox.kvx", data: kvxBox(16, 16, 16) },
    { name: "models/hudskin.png", data: encodePNG(16, 16, skin) },
  ]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The box, low in the middle of the frame where the pistol sprite would be: about half of it green, 0.445-0.555 across and 0.574-0.786 down in GL.
export const BOXES = { MAP01: {
  weapon: { box: [0.42, 0.55, 0.58, 0.82], ceiling: 3 },
} };

// [rc4l] The harness hides the weapon everywhere, so each side shows it again.
const WEAPON = ["r_drawplayersprites 1"];
export const SIDES = [{ name: "gl", gl: true, commands: WEAPON }, { name: "vk", commands: WEAPON },
  { name: "standalone", cvars: { fua_dg_standalone: 1 }, commands: WEAPON }, { name: "nogl", noGl: true, commands: WEAPON }];

runScenarioIfMain(import.meta.url, () => ({ name: "hudmodel", wad: buildHudModelPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));

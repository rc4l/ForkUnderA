// [rc4l] A skybox room that is all sky, its floor and ceiling sky and its walls horizon lines, GL against Vulkan: exit 0 when Vulkan's sky shows the level's sky through it as GL's does.
//
// usage:  node scenarios/skyboxopen.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { encodePNG } from "../src/pngcore.mjs";

// [rc4l] A six-face GLDEFS skybox as the level's sky, each face one flat colour so a face that goes missing is plain to see.
const FACES = [["OBOXN", [200, 60, 60]], ["OBOXE", [60, 200, 60]], ["OBOXS", [60, 60, 200]], ["OBOXW", [200, 200, 60]],
  ["OBOXT", [60, 200, 200]], ["OBOXB", [200, 60, 200]]];
const face = (rgb) => { const w = 8, px = Buffer.alloc(w * w * 3); for (let i = 0; i < w * w; i++) px.set(rgb, i * 3); return encodePNG(w, w, px); };
const GLDEFS = `skybox OBOX\n{\n${FACES.map(([n]) => `\t${n}`).join("\n")}\n}\n`;
const MAPINFO = `map MAP01 "Open skybox"\n{\n\tsky1 = "OBOX", 0\n}\n`;

// [rc4l] WDI16's beach: the SkyViewpoint stands in a room with nothing in it but sky, which GL fills with the level's own six-face skybox and the backend left black.
export function buildSkyboxOpenWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.room(4000, 4000, 4512, 4512, { heightfloor: 0, heightceiling: 256, texturefloor: "F_SKY1", textureceiling: "F_SKY1",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } }));
  m.thing({ x: 4256, y: 4256, height: 128, angle: 0, type: THING.SkyViewpoint });
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad([{ name: "TX_START", data: "" }, ...FACES.map(([name, rgb]) => ({ name, data: face(rgb) })), { name: "TX_END", data: "" },
    { name: "GLDEFS", data: GLDEFS }, { name: "MAPINFO", data: MAPINFO }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Up at the open sky over the court.
export const VANTAGES = [{ name: "up", at: { x: 512, y: 128, z: 0, angle: 90, pitch: -30 } }];

// [rc4l] The top of the frame, all sky.
export const BOXES = { MAP01: { sky: { box: [0.1, 0.05, 0.9, 0.4], ceiling: 16 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skyboxopen", wad: buildSkyboxOpenWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

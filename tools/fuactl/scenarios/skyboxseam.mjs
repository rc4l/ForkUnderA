// [rc4l] A six-face GLDEFS skybox seen at its corners, GL against Vulkan: exit 0 when Vulkan's faces meet without a seam, as GL's clamped faces do.
//
// usage:  node scenarios/skyboxseam.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { encodePNG } from "../src/pngcore.mjs";

// [rc4l] Every face grey with one red column at its left edge, so a sampler that wraps bleeds red into the right edge of each face, the seam WDI03's sky showed.
const FACE = (() => {
  const w = 8, rgb = Buffer.alloc(w * w * 3, 100);
  for (let y = 0; y < w; y++) rgb.set([255, 0, 0], y * w * 3);
  return encodePNG(w, w, rgb);
})();
const FACES = ["TBOXN", "TBOXE", "TBOXS", "TBOXW", "TBOXT", "TBOXB"];

const GLDEFS = `skybox TBOX
{
${FACES.map((f) => `\t${f}`).join("\n")}
}
`;
const MAPINFO = `map MAP01 "Skybox"
{
\tsky1 = "TBOX", 0
}
`;

export function buildSkyboxSeamWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 45, type: 1 });
  return wad([{ name: "TX_START", data: "" }, ...FACES.map((name) => ({ name, data: FACE })), { name: "TX_END", data: "" },
    { name: "GLDEFS", data: GLDEFS }, { name: "MAPINFO", data: MAPINFO }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Looking up into two of the box's corners, where its walls meet each other and the top.
export const VANTAGES = [
  { name: "corner-ne", at: { x: 256, y: 256, z: 0, angle: 45, pitch: -45 } },
  { name: "corner-sw", at: { x: 256, y: 256, z: 0, angle: 225, pitch: -45 } },
];

// [rc4l] The sky above the walls, in small blocks so a seam a pixel wide still moves the number.
export const BOXES = { MAP01: { sky: { box: [0.0, 0.12, 1.0, 0.45], ceiling: 1, block: 4 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skyboxseam", wad: buildSkyboxSeamWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

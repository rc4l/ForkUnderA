// [rc4l] GLDEFS hardware shaders on two wall textures, GL against Vulkan: exit 0 when Vulkan runs each mod shader on its texture as GL does, one written as ProcessTexel and one in the older Process(color) form.
//
// usage:  node scenarios/hwshader.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const GLDEFS = `HardwareShader Texture "STARTAN2"
{
\tShader "shaders/red.fp"
\tSpeed 1.0
}
HardwareShader Texture "BRICK1"
{
\tShader "shaders/old.fp"
}
`;

// [rc4l] Red and a little blue only, from the texel getTexel gives.
const RED = `uniform float timer;
vec4 ProcessTexel()
{
\tvec4 t = getTexel(vTexCoord.st);
\treturn vec4(t.r, 0.0, t.b * 0.5, t.a);
}
`;

// [rc4l] The old form GL still loads: Process(color), reading gl_TexCoord[0], its green as yellow.
const OLD = `vec4 Process(vec4 color)
{
\tvec4 t = getTexel(gl_TexCoord[0].st) * color;
\treturn vec4(t.g, t.g, 0.0, t.a);
}
`;

// [rc4l] A pk3, since GL finds a shader by its full path and a wad's lumps have none.
export function buildHwShaderPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : edge === "south" ? "BRICK1" : "COMPBLUE" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "gldefs.txt", data: GLDEFS },
    { name: "shaders/red.fp", data: RED },
    { name: "shaders/old.fp", data: OLD },
  ]);
}

export const VANTAGES = [
  { name: "north", at: { x: 256, y: 256, z: 0, angle: 90, pitch: 0 } },
  { name: "south", at: { x: 256, y: 256, z: 0, angle: 270, pitch: 0 } },
];

// [rc4l] The shaded wall straight ahead, above the weapon.
export const BOXES = { MAP01: {
  processTexel: { vantage: "north", box: [0.3, 0.2, 0.7, 0.6], ceiling: 2 },
  process: { vantage: "south", box: [0.3, 0.2, 0.7, 0.6], ceiling: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "hwshader", wad: buildHwShaderPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

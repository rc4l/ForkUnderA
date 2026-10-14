// [rc4l] A fuzzy actor under every r_fuzztype shader, GL against Vulkan at one pinned frame clock: exit 0 when Vulkan draws GL's fuzz shaders, not plain translucency.
//
// usage:  node scenarios/fuzz.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

const FUZZY_PILLAR = 15003;

// [rc4l] A tech pillar drawn fuzzy, as a spectre is, against a bright wall; MAP01 to MAP07 draw it under r_fuzztype 1 to 7.
export function buildFuzzWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 300, angle: 90, type: FUZZY_PILLAR });
  m.thing({ x: 256, y: 160, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "DECORATE", data: `actor ZXFuzzyPillar : TechPillar ${FUZZY_PILLAR}\n{\n  RenderStyle Fuzzy\n}\n` }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 160, z: 0, angle: 90, pitch: 0 } }];

export const BOXES = { MAP01: { pillar: { box: [0.45, 0.42, 0.55, 0.72], ceiling: 3 } } };

// [rc4l] The fuzz type the scenario draws, 5 (jagged, which also splats the texture) unless FUA_FUZZTYPE says.
const FUZZTYPE = process.env.FUA_FUZZTYPE ? Number(process.env.FUA_FUZZTYPE) : 5;
export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, cvars: { r_fuzztype: FUZZTYPE, fua_debug_frameclock: 4321 } }));

runScenarioIfMain(import.meta.url, () => ({ name: "fuzz", wad: buildFuzzWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));

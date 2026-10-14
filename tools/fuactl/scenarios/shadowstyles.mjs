// [rc4l] The styles GL draws with its DST_COLOR fallback blend or fogs to black, GL against Vulkan from one frame: exit 0 when Vulkan draws a fuzzy actor under r_fuzztype 0 as GL's dark shadow, a fuzzy decal as GL's multiply, and an additive actor in coloured fog as GL does.
//
// usage:  node scenarios/shadowstyles.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { kvxBox } from "../src/kvx.mjs";

const FUZZY_PILLAR = 15004;
const ADD_PILLAR = 15005;
const SUB_MODEL = 15006;
const FUZZY_MODEL = 15007;
// [rc4l] The Decal thing, which glues its decal to the wall behind it at spawn.
const DECAL_THING = 9200;

const DECORATE = `actor ZXShadowPillar : TechPillar ${FUZZY_PILLAR}\n{\n  RenderStyle Fuzzy\n}\n`
  + `actor ZXSubModel : Column ${SUB_MODEL}\n{\n  RenderStyle Subtract\n  Alpha 1.0\n}\n`
  + `actor ZXFuzzyModel : Column ${FUZZY_MODEL}\n{\n  RenderStyle Fuzzy\n}\n`
  + `actor ZXAddPillar : TechPillar ${ADD_PILLAR}\n{\n  RenderStyle Add\n  Alpha 1.0\n}\n`;

// [rc4l] A bright graphic with clear edges drawn fuzzy, big enough to fill a box: GL multiplies it into the wall, so its bright texels all but vanish.
const DECALDEF = `decal FuaFuzzy 3\n{\n\tpic BAL1A0\n\tfuzzy\n\tx-scale 6.0\n\ty-scale 6.0\n}\n`;

// [rc4l] One room, bright and plain on MAP01 and dimmer in a blue fade on MAP02, where GL thins the shadow with distance and fogs additive light to black; MAP03 stands models in the pillars' places, which GL subtracts and shadows too.
function room(lightlevel, fadecolor, left = FUZZY_PILLAR, right = ADD_PILLAR) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel, ...(fadecolor ? { fadecolor } : {}) }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  m.thing({ x: 400, y: 700, angle: 90, type: left });
  m.thing({ x: 624, y: 700, angle: 90, type: right });
  // [rc4l] Facing south, so it shoots its decal north onto the far wall above the pillars.
  m.thing({ x: 512, y: 1000, height: 170, angle: 270, type: DECAL_THING, arg0: 3 });
  return m;
}

export function buildShadowStylesWad() {
  return wad([{ name: "DECORATE", data: DECORATE }, { name: "DECALDEF", data: DECALDEF },
    ...mapLumps("MAP01", room(255, 0)), ...mapLumps("MAP02", room(160, 0x3060c0)), ...mapLumps("MAP03", room(255, 0, FUZZY_MODEL, SUB_MODEL)),
    // [rc4l] The column's sprite a pale voxel post, so only MAP03's things are models; one voxel column scaled up, so a face is one layer and back faces show whether they are drawn.
    { name: "VOXELDEF", data: 'COLUA = "ZXBOX" { scale = 32.0 }\n' },
    { name: "VX_START", data: "" }, { name: "ZXBOX", data: kvxBox(1, 1, 3, { color: [57, 57, 57] }) }, { name: "VX_END", data: "" }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 512, y: 460, z: 0, angle: 90, pitch: 0 } }];

const PLACES = {
  shadow: { box: [0.29, 0.20, 0.36, 0.62], ceiling: 3, block: 4 },
  additive: { box: [0.645, 0.20, 0.705, 0.62], ceiling: 3, block: 4 },
  decal: { box: [0.43, 0.20, 0.57, 0.50], ceiling: 3, block: 4 },
};
export const BOXES = { MAP01: PLACES, MAP02: PLACES, MAP03: {
  shadowModel: { box: [0.27, 0.20, 0.38, 0.66], ceiling: 3, block: 4 },
  subtractModel: { box: [0.62, 0.20, 0.73, 0.66], ceiling: 3, block: 4 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "shadowstyles", wad: buildShadowStylesWad(), maps: ["MAP01", "MAP02", "MAP03"],
  vantages: VANTAGES, boxes: BOXES, commands: ["r_fuzztype 0"] }), compareSameFrame);

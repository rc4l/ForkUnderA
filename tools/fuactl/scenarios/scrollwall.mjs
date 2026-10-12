// [rc4l] A scrolling wall over a scrolling floor, GL against Vulkan from one frame: exit 0 when Vulkan's textures stand where GL's do after the scrollers have run.
//
// usage:  node scenarios/scrollwall.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { SECTOR_TYPE } from "../src/doomnums.mjs";

// [rc4l] Scroll_Texture_Left, as AOW03's 101 scrolling walls use it, from the action special table.
const SCROLL_TEXTURE_LEFT = 100;

// [rc4l] Queue #42's follow-up: a scroller moves a texture inside the vertices the bake made, by a shift in the piece the shader adds.
export function buildScrollWallWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192, special: SECTOR_TYPE.Scroll_East_Fast }, (edge) => (edge === "north"
    ? { side: { texturemiddle: "COMPUTE2" }, line: { special: SCROLL_TEXTURE_LEFT, arg0: 96 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 600, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Square on to the scrolling wall, tilted down so the scrolling floor fills the lower half.
export const VANTAGES = [{ name: "scroll", at: { x: 512, y: 860, z: 0, angle: 90, pitch: 12 } }];

export const BOXES = { MAP01: {
  wall: { box: [0.15, 0.05, 0.85, 0.4], ceiling: 4 },
  floor: { box: [0.15, 0.7, 0.85, 0.95], ceiling: 4 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "scrollwall", wad: buildScrollWallWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);

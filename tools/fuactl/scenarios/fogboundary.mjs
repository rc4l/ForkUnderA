// [rc4l] A fogged room opening onto a clear one, seen from inside the fog, GL against Vulkan: exit 0 when Vulkan lays GL's fog boundary sheet over the opening.
//
// usage:  node scenarios/fogboundary.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A dim blue-fogged room with a bright clear one beyond a wide opening in its north wall.
export function buildFogBoundaryWad() {
  const m = new UdmfMap();
  const flats = { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
  const fog = m.sector({ ...flats, lightlevel: 128, fadecolor: 0x406080 });
  const clear = m.sector({ ...flats, lightlevel: 200 });
  const wall = { texturemiddle: "STARTAN2" }, far = { texturemiddle: "BROWN1" };
  // [rc4l] Each room walked clockwise, so it is on every wall's right, the line at y 1024 joining them.
  m.wall(0, 0, 0, 1024, fog, wall);
  m.opening(0, 1024, 512, 1024, fog, clear);
  m.wall(512, 1024, 512, 0, fog, wall);
  m.wall(512, 0, 0, 0, fog, wall);
  m.wall(0, 1024, 0, 2048, clear, far);
  m.wall(0, 2048, 512, 2048, clear, far);
  m.wall(512, 2048, 512, 1024, clear, far);
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "opening", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The clear room seen through the opening, where only the sheet fogs it.
export const BOXES = { MAP01: { beyond: { box: [0.3, 0.3, 0.7, 0.6], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "fogboundary", wad: buildFogBoundaryWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

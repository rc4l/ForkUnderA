// [rc4l] A sky wall that starts below the eye, over a closed sector, with a lit room behind it, GL against Vulkan: exit 0 when Vulkan paints the skybox over the whole wall as GL does.
//
// usage:  node scenarios/skywallbelow.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI18's fence: GL draws sky from a closed sky sector's height up, and the backend cut that sky at eye level, so the level behind showed through the part below.
export function buildSkyWallBelowWad() {
  const m = new UdmfMap();
  const near = m.sector({ heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 160 });
  const closed = m.sector({ heightfloor: 16, heightceiling: 16, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 160 });
  const far = m.sector({ heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5", lightlevel: 255 });
  const tiers = { texturebottom: "BROWN1", texturetop: "BROWN1" };
  const wall = (x1, y1, x2, y2, s, tex) => m.wall(x1, y1, x2, y2, s, { texturemiddle: tex });
  // [rc4l] The player's room, open north onto the closed strip.
  wall(0, 0, 0, 512, near, "STARTAN2");
  m.opening(0, 512, 1024, 512, near, closed, { texturebottom: "BROWN1" }, tiers);
  wall(1024, 512, 1024, 0, near, "STARTAN2");
  wall(1024, 0, 0, 0, near, "STARTAN2");
  // [rc4l] The closed strip, floor and sky ceiling both at 16, which GL's clipper treats as solid.
  wall(0, 512, 0, 576, closed, "BROWN1");
  m.opening(0, 576, 1024, 576, closed, far, {}, tiers);
  wall(1024, 576, 1024, 512, closed, "BROWN1");
  // [rc4l] The bright room GL's walk never reaches.
  wall(0, 576, 0, 1536, far, "REDWALL");
  wall(0, 1536, 1024, 1536, far, "REDWALL");
  wall(1024, 1536, 1024, 576, far, "REDWALL");
  m.skyboxRoom(4000, 4000);
  m.thing({ x: 512, y: 384, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Level, at the strip from close by, so the sky wall reaches well below the horizon.
export const VANTAGES = [{ name: "strip", at: { x: 512, y: 384, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The rows between the horizon and the top of the strip's bottom tier.
export const BOXES = { MAP01: { strip: { box: [0.2, 0.52, 0.8, 0.6], ceiling: 6 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skywallbelow", wad: buildSkyWallBelowWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

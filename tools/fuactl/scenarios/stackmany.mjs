// [rc4l] Three stacked-sector ceilings side by side, each onto its own upper room, GL against Vulkan: exit 0 when Vulkan shows all three through their ceilings as GL does, past the two stacked views a frame it used to stop at.
//
// usage:  node scenarios/stackmany.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] Lower sectors A, B and C in a row under 128-high ceilings, each stacked onto an upper room displaced by its own amount with walls of its own texture.
export function buildStackManyWad() {
  const m = new UdmfMap();
  const low = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 };
  const a = m.sector({ ...low }), b = m.sector({ ...low }), c = m.sector({ ...low });
  const wall = { texturemiddle: "STARTAN2" };
  m.wall(0, 0, 0, 512, a, wall);
  m.wall(0, 512, 512, 512, a, wall);
  m.wall(512, 0, 0, 0, a, wall);
  m.opening(512, 512, 512, 0, a, b);
  m.wall(512, 512, 1024, 512, b, wall);
  m.wall(1024, 0, 512, 0, b, wall);
  m.opening(1024, 512, 1024, 0, b, c);
  m.wall(1024, 512, 1536, 512, c, wall);
  m.wall(1536, 512, 1536, 0, c, wall);
  m.wall(1536, 0, 1024, 0, c, wall);
  const views = [{ x0: 0, dx: 4096, tex: "BRICK1", tid: 7 }, { x0: 512, dx: 8192, tex: "COMPBLUE", tid: 8 }, { x0: 1024, dx: 12288, tex: "STARTAN3", tid: 9 }];
  for (const v of views) {
    m.room(v.x0 + v.dx, 0, v.x0 + v.dx + 512, 512, { heightfloor: 128, heightceiling: 640, texturefloor: "FLOOR4_8",
      textureceiling: "F_SKY1", lightlevel: 255 }, () => ({ side: { texturemiddle: v.tex } }));
    m.thing({ x: v.x0 + 64, y: 64, angle: 0, type: THING.LowerStackLookOnly, id: v.tid });
    m.thing({ x: v.x0 + v.dx + 64, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: v.tid });
  }
  m.thing({ x: 768, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Looking up across all three ceilings from the middle of the south wall.
export const VANTAGES = [{ name: "up", at: { x: 768, y: 64, z: 0, angle: 90, pitch: -35 } }];

// [rc4l] The middle ceiling's room overhead, and the side rooms' walls in the lower corners.
export const BOXES = { MAP01: {
  left: { box: [0.0, 0.6, 0.2, 0.82], ceiling: 2 },
  middle: { box: [0.35, 0.05, 0.65, 0.4], ceiling: 2 },
  right: { box: [0.8, 0.6, 1.0, 0.82], ceiling: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "stackmany", wad: buildStackManyWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

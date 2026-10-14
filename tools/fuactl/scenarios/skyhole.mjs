// [rc4l] A one-sided wall with no texture under a sky, GL against Vulkan: exit 0 when Vulkan shows the sky through the wall as GL does, not the level beyond, and still the things GL draws behind it.
//
// usage:  node scenarios/skyhole.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI18's fence top (linedef 17638): GL's walk clips everything behind a one-sided wall and its sky portal fills the screen first, so the bare wall shows sky, while the backend drew the level behind it.
// [rc4l] WDI18's forest (linedef 14495): GL draws every thing in a sector once its walk reaches any of it, so a pillar in the yard's own sector beyond the bare wall shows through it over the sky.
function yardMap({ pillarBehind, skybox }) {
  const m = new UdmfMap();
  const yard = m.sector({ heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 160 });
  const wall = (x1, y1, x2, y2, s, tex) => m.wall(x1, y1, x2, y2, s, tex ? { texturemiddle: tex } : {});
  if (pillarBehind) {
    // [rc4l] One sector: the yard, a corridor at its north-east corner, and a field beyond the bare wall the corridor reaches.
    wall(0, 0, 0, 512, yard, "STARTAN2");
    wall(0, 512, 768, 512, yard, null);
    wall(768, 512, 768, 640, yard, "STARTAN2");
    wall(768, 640, 0, 640, yard, "REDWALL");
    wall(0, 640, 0, 1536, yard, "REDWALL");
    wall(0, 1536, 1024, 1536, yard, "REDWALL");
    wall(1024, 1536, 1024, 0, yard, "STARTAN2");
    wall(1024, 0, 0, 0, yard, "STARTAN2");
    m.thing({ x: 384, y: 800, angle: 270, type: THING.TechPillar });
  } else {
    // [rc4l] The yard, its north wall left without a texture.
    wall(0, 0, 0, 512, yard, "STARTAN2");
    wall(0, 512, 1024, 512, yard, null);
    wall(1024, 512, 1024, 0, yard, "STARTAN2");
    wall(1024, 0, 0, 0, yard, "STARTAN2");
    // [rc4l] A bright room beyond it, closed on its own, which only a renderer without GL's clipper can see.
    m.room(0, 640, 1024, 1536, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5",
      lightlevel: 255 }, () => ({ side: { texturemiddle: "REDWALL" } }));
  }
  if (skybox) m.skyboxRoom(4000, 4000);
  m.thing({ x: 512, y: 256, angle: 90, type: 1 });
  return m;
}

// [rc4l] MAP01 the room behind under a skybox; MAP02 the pillar under a skybox, MAP03 under the plain sky.
export function buildSkyHoleWad() {
  return wad([...mapLumps("MAP01", yardMap({ pillarBehind: false, skybox: true })),
    ...mapLumps("MAP02", yardMap({ pillarBehind: true, skybox: true })),
    ...mapLumps("MAP03", yardMap({ pillarBehind: true, skybox: false }))]);
}

// [rc4l] Facing the bare wall from the middle of the yard, a little down so the room's floor would show through it.
export const VANTAGES = [{ name: "wall", at: { x: 512, y: 256, z: 0, angle: 90, pitch: 10 } }];

// [rc4l] The wall's span below the sky over it, and on the pillar maps what GL draws through it: the pillar, the field's floor and the far wall.
const WALL = { box: [0.1, 0.35, 0.9, 0.65], ceiling: 4 };
const BEHIND = { box: [0.35, 0.2, 0.85, 0.45], ceiling: 4 };
export const BOXES = { MAP01: { wall: WALL }, MAP02: { behind: BEHIND }, MAP03: { behind: BEHIND } };

runScenarioIfMain(import.meta.url, () => ({ name: "skyhole", wad: buildSkyHoleWad(), maps: ["MAP01", "MAP02", "MAP03"],
  vantages: VANTAGES, boxes: BOXES }));

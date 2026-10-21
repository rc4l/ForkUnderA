// [rc4l] More sprites in view than a small level's piece buffer kept records for behind its own, GL against Vulkan: exit 0 when every sprite draws its own image.
//
// usage:  node scenarios/spritecrowd.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] nuts.wad with Complex Doom: 10,600 monsters on a 192-piece map overran the records kept behind the level's, and every sprite past them drew with the last record's image, so rows showed as one wrong monster.
// [rc4l] Fourteen still decorations: which kinds lose their records follows the materials' addresses, so with three the wrong one was sometimes too small to judge, and with fourteen several are wrong whichever they are.
const KINDS = [THING.TallGreenColumn, THING.ShortGreenColumn, THING.TallRedColumn, THING.ShortRedColumn, THING.SkullColumn, THING.TechPillar,
  THING.Column, THING.TorchTree, THING.BigTree, THING.Stalagtite, THING.Candelabra, THING.DeadStick, THING.HeadOnAStick, THING.HeadsOnAStick];
// [rc4l] 7,068 of them, far enough off that the whole crowd is in view.
const SPACING = 16;
const ROWS = 62, COLS = 114;

export function buildSpriteCrowdWad() {
  const m = new UdmfMap();
  m.room(0, 0, 2048, 2200, { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (let r = 0; r < ROWS; r++)
    for (let c = 0; c < COLS; c++)
      m.thing({ x: 112 + c * SPACING, y: 1000 + r * SPACING, angle: 90, type: KINDS[(r + c) % KINDS.length] });
  m.thing({ x: 1024, y: 40, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "crowd", at: { x: 1024, y: 40, z: 0, angle: 90, pitch: 0 } }];

export const BOXES = { MAP01: { crowd: { box: [0.05, 0.35, 0.95, 0.65], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "spritecrowd", wad: buildSpriteCrowdWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));

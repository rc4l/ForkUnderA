// [rc4l] A wall and a sprite with GLDEFS brightmaps in a dark room, bindless against a binding per material: exit 0 when the bindless array lights them as well.
//
// usage:  node scenarios/brightmap.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] An all-white brightmap, so whatever wears it is lit fully wherever it is drawn.
const WHITE = encodePNG(8, 8, Buffer.alloc(8 * 8 * 3, 255));

// [rc4l] STARTAN2 on the walls and the tall lamp's first frame both take it, defined in the wad's own GLDEFS.
const GLDEFS = `brightmap texture STARTAN2 { map BMWHITE }
brightmap sprite COLUA0 { map BMWHITE }
`;

export function buildBrightmapWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 48 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 200, y: 300, angle: 270, type: THING.Column });
  return wad([{ name: "TX_START", data: "" }, { name: "BMWHITE", data: WHITE }, { name: "TX_END", data: "" },
    { name: "GLDEFS", data: GLDEFS }, ...mapLumps("MAP01", m)]);
}

export const VANTAGES = [{ name: "north", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The far wall above the lamp's head, and the lamp itself.
export const BOXES = { MAP01: {
  wall: { box: [0.6, 0.25, 0.9, 0.45], ceiling: 0.05 },
  lamp: { box: [0.3, 0.35, 0.45, 0.65], ceiling: 0.05 },
} };

// [rc4l] A binding per material carries each one's brightmap and is the reference; the bindless array must match it.
export const SIDES = [{ name: "bindless", cvars: { fua_dg_bindless: "1" } }, { name: "per-material", cvars: { fua_dg_bindless: "0" } }];

runScenarioIfMain(import.meta.url, () => ({ name: "brightmap", wad: buildBrightmapWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));

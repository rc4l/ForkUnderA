// [rc4l] A pool split into two sectors whose 3D-floor waters differ only below the floor, against the same pool in one piece: exit 0 when the seam draws no wall.
//
// usage:  node scenarios/waterseam.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] wdiexsec's dock is the inner sector: its water reaches -154, the pool's -194, and both surfaces sit at -80.
const POOL_TAG = 9, INNER_TAG = 10;
const POOL = { heightfloor: -154, heightceiling: 256, texturefloor: "FLAT1", textureceiling: "CEIL3_5", lightlevel: 152 };

// [rc4l] A swimmable water slab of `alpha` for every sector tagged `tag`, from -80 down to `bottom`, lit `light` (and `color`) under it, in a control room at (x, y); its COMPBLUE sides are what a wrong seam shows.
function water(m, x, y, tag, bottom, { alpha, light, color }) {
  m.room(x, y, x + 64, y + 64, { heightfloor: bottom, heightceiling: -80, texturefloor: "FLAT1", textureceiling: "GRNROCK",
    lightlevel: light, ...(color === undefined ? {} : { lightcolor: color }) }, (edge) => (edge === "south"
    ? { side: { texturemiddle: "COMPBLUE" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: tag, arg1: 2, arg2: 0, arg3: alpha } }
    : { side: { texturemiddle: "COMPBLUE" } }));
}

// [rc4l] The walkway and its pool of half-clear water, with the inner sector cut out of it when `split` is set, and `things` standing in it.
export function pool({ split = false, alpha = 128, light = 128, color, things = [] } = {}) {
  const m = new UdmfMap();
  const walk = m.room(0, 0, 1024, 1024, { heightfloor: -42, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 152 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  const deep = m.block(256, 384, 768, 896, { ...POOL, id: POOL_TAG }, walk, () => ({}), () => ({ texturebottom: "STARTAN2" }));
  water(m, 2000, 2000, POOL_TAG, -194, { alpha, light, color });
  if (split) {
    m.block(384, 512, 640, 768, { ...POOL, id: INNER_TAG }, deep);
    water(m, 2100, 2000, INNER_TAG, -170, { alpha, light, color });
  }
  m.thing({ x: 512, y: 250, angle: 90, type: 1 });
  for (const t of things) m.thing(t);
  return m;
}

export function buildWaterSeamWad() {
  return wad([...mapLumps("MAP01", pool({ split: true })), ...mapLumps("MAP02", pool())]);
}

// [rc4l] From the pool's south edge, looking down through the water at the inner sector.
export const VANTAGES = [{ name: "above", at: { x: 512, y: 370, z: -42, angle: 90, pitch: 40 } }];

// [rc4l] The pool's water, where the seam's wall showed through as a darker box.
export const BOXES = { MAP01: { water: { box: [0.15, 0.3, 0.85, 0.95], ceiling: 0.05 } } };

// [rc4l] The same pool in one piece is what a clipped seam must look like, so the second side loads MAP02.
export const SIDES = [{ name: "split" }, { name: "whole", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "waterseam", wad: buildWaterSeamWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));

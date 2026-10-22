// [rc4l] Rooms whose light changes every tic in each way the sector light table carries (a flicker, a glow, a script, a colour and fade, a desaturation, plane lights, side lights in and out of fog) and one a 3D floor lights, GL against Vulkan from one frame under the default light mode, light modes 0 and 8 and radial fog: exit 0 when Vulkan lights every room as GL does.
//
// usage:  node scenarios/lighttable.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

// [rc4l] UDMF's Doom sector types, 64 past Doom's own numbers.
const FLICKER = 65, GLOW = 72, SLOW_SYNC = 76, FAST_SYNC = 77, FIRE = 81;
const SCRIPT_TAG = 10, COLOUR_TAG = 11, DESAT_TAG = 12, XFLOOR_TAG = 13;
const flats = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
const plain = { side: { texturemiddle: "STARTAN2" } };

// [rc4l] Each room's sector and the sides its walls take, edge by edge.
export const ROOMS = [
  { name: "flicker", sector: { lightlevel: 192, special: FLICKER } },
  { name: "glow", sector: { lightlevel: 192, special: GLOW } },
  { name: "script", sector: { lightlevel: 96, id: SCRIPT_TAG } },
  { name: "colour", sector: { lightlevel: 160, special: FIRE, id: COLOUR_TAG } },
  { name: "desat", sector: { lightlevel: 160, special: FLICKER, id: DESAT_TAG } },
  { name: "planes", sector: { lightlevel: 160, special: FAST_SYNC, lightfloor: 48, lightceiling: 220, lightceilingabsolute: true } },
  { name: "sides", sector: { lightlevel: 160, special: SLOW_SYNC },
    walls: { north: { light: 48 }, east: { light: 200, lightabsolute: true }, west: { light: 32, lightfog: true } } },
  { name: "fogsides", sector: { lightlevel: 160, special: FLICKER, fadecolor: 0x203040 },
    walls: { north: { light: 48 }, west: { light: 48, lightfog: true } } },
  { name: "xfloor", sector: { lightlevel: 192, special: FLICKER, id: XFLOOR_TAG }, xfloor: true },
];

// [rc4l] Where room i stands: rooms 1024 apart along x.
const roomX = (i) => i * 1024;

// [rc4l] The rooms named by `names`, each 512 square, and the script that changes the script, colour and desaturation rooms after the level is baked.
export function buildLightTableMap(names = ROOMS.map((r) => r.name)) {
  const m = new UdmfMap();
  ROOMS.forEach((r, i) => {
    if (!names.includes(r.name)) return;
    const x = roomX(i);
    const s = m.room(x, 0, x + 512, 512, { ...flats, ...r.sector }, (edge) => ({ side: { texturemiddle: "STARTAN2", ...((r.walls || {})[edge] || {}) } }));
    // [rc4l] A dark square in its far corner, the neighbour Doom's flickers, glows and strobes dim to, which a room alone would hold at its own light.
    m.block(x + 400, 400, x + 464, 464, { ...flats, lightlevel: 32 }, s);
    // [rc4l] A 3D floor ledge across the room's middle, its control sector flickering on its own out of sight.
    if (r.xfloor) {
      m.room(x, 2000, x + 64, 2064, { heightfloor: 88, heightceiling: 104, texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 224, special: FLICKER },
        (edge) => (edge === "west" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: XFLOOR_TAG, arg1: 1, arg2: 0, arg3: 255 } } : plain));
    }
  });
  m.thing({ x: 256, y: 96, angle: 90, type: 1 });
  const behavior = acs0([PCD.DELAYDIRECT, 18,
    ...callSpecial(SPECIAL.Light_ChangeToValue, [SCRIPT_TAG, 224]),
    ...callSpecial(SPECIAL.Sector_SetColor, [COLOUR_TAG, 255, 128, 64, 0]),
    ...callSpecial(SPECIAL.Sector_SetFade, [COLOUR_TAG, 32, 48, 64]),
    ...callSpecial(SPECIAL.Sector_SetColor, [DESAT_TAG, 255, 255, 255, 160]),
    PCD.TERMINATE]);
  return { map: m, behavior };
}

// [rc4l] MAP01 every room, MAP02 all but the 3D floor's, whose bands keep their baked light.
export function buildLightTableWad() {
  const all = buildLightTableMap();
  const table = buildLightTableMap(ROOMS.filter((r) => !r.xfloor).map((r) => r.name));
  return wad([...mapLumps("MAP01", all.map, { behavior: all.behavior }), ...mapLumps("MAP02", table.map, { behavior: table.behavior })]);
}

// [rc4l] Facing each room's north wall from its south half, its floor, ceiling and three walls in frame.
export const VANTAGES = ROOMS.map((r, i) => ({ name: r.name, at: { x: roomX(i) + 256, y: 96, z: 0, angle: 90, pitch: 0 } }));

// [rc4l] Each room's whole view above the status bar.
export const BOXES = { MAP01: { view: { box: [0, 0.02, 1, 0.82], ceiling: 2 } } };

// [rc4l] The settings each comparison runs under, set once the level is up, so a baked light that missed the change shows.
export const SETTINGS = [
  { name: "lightmode 3", commands: [] },
  { name: "lightmode 0", commands: ["r_lightmode 0"] },
  { name: "lightmode 8", commands: ["r_lightmode 8"] },
  { name: "radial fog", commands: ["r_fogmode 2"] },
];

// [rc4l] compareSameFrame under each setting, its lines named by the setting.
export async function compareEachSetting(opts) {
  const lines = [];
  let failed = 0, refused = false;
  for (const s of SETTINGS) {
    const r = await compareSameFrame({ ...opts, name: `${opts.name}_${s.name.replace(/ /g, "")}`, commands: s.commands, settleTics: 35 });
    lines.push(...r.lines.map((l) => `${s.name}: ${l}`));
    failed += r.failed;
    refused = refused || !!r.refused;
  }
  return { lines, failed, refused };
}

runScenarioIfMain(import.meta.url, () => ({ name: "lighttable", wad: buildLightTableWad(), maps: ["MAP01"], vantages: VANTAGES, boxes: BOXES }),
  compareEachSetting);

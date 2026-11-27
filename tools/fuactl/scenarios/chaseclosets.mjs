// [Doruk-Aksoy] Six packed monster closets, each at the end of its own 4,096-unit corridor off the player's room, opened and 2 s later woken by sound; 30 s after the wake each closet's monsters still inside are counted against a ceiling, so a chase change that stalls crowds fails (features/chase-trim).
//
// usage:  node scenarios/chaseclosets.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acsScripts, callSpecial, ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] How long after the doors start opening the closets are woken, and how long after that they are counted.
export const WAKE_TICS = 70;
export const COUNT_AFTER_TICS = 30 * 35;
// [Doruk-Aksoy] The script the side pukes once its settings are in, so a wake never lands while the launch's notarget still holds.
export const RUN_SCRIPT = 1;
const ROOM_X1 = 1024, DOOR_X = ROOM_X1 + 4096, CLOSET_X0 = DOOR_X + 16, SPACING = 640, HALF_W = 96;
// [Doruk-Aksoy] Sector tags: every door (opened by one Door_Open) and every closet.
export const TAG = { door: 1, closet: 2 };
const RADIUS = { 3001: 20, 66: 20, 64: 20, 67: 48, 68: 64 };

// [Doruk-Aksoy] Rows from the back wall forward, a type or a list cycled across the row; `left` is the most still inside after 30 s: one where the closet always empties, about twice the 12-seed mean for arachnotrons, and two out for the mancubi and the mix, whose big monsters drain unevenly (4 to 12 mancubi left run to run).
export const CLOSETS = [
  { name: "imps", tid: 101, size: 256, rows: Array(6).fill(3001), left: 1 },
  { name: "revenants", tid: 102, size: 256, rows: Array(6).fill(66), left: 1 },
  { name: "mancubi", tid: 103, size: 400, rows: Array(4).fill(67), left: 14 },
  { name: "arachnotrons", tid: 104, size: 520, rows: Array(3).fill(68), left: 4 },
  { name: "archviles", tid: 105, size: 256, rows: Array(6).fill(64), left: 1 },
  { name: "mix", tid: 106, size: 520, rows: [[3001, 66, 64], 67, [66, 3001, 64], 68, [64, 66, 3001], 67], left: 44 },
];

export const cvarOf = (closet) => `zx_closet_${closet.name}`;

// [Doruk-Aksoy] Each row's monsters across the closet, the back row against its far wall, as packed as their radii allow.
function placeCloset(m, closet, yc) {
  const x1 = CLOSET_X0 + closet.size, y0 = yc - closet.size / 2, y1 = yc + closet.size / 2;
  let x = x1 - 2;
  for (const row of closet.rows) {
    const types = Array.isArray(row) ? row : [row];
    const r = Math.max(...types.map((t) => RADIUS[t]));
    x -= r;
    const step = 2 * r + 2, count = Math.floor((closet.size - 4) / step);
    for (let k = 0; k < count; k++) m.thing({ x, y: y0 + 2 + r + k * step, angle: 0, type: types[k % types.length], id: closet.tid });
    x -= r + 2;
  }
  return { x1, y0, y1 };
}

// [Doruk-Aksoy] The room, a corridor, door and closet per entry of CLOSETS, and the player facing east.
export function buildClosetsMap() {
  const m = new UdmfMap();
  const lit = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
  const tex = { texturemiddle: "STARTAN2" };
  const H = SPACING * CLOSETS.length;
  const P = m.sector(lit);
  m.wall(0, 0, 0, H, P, tex); m.wall(0, H, ROOM_X1, H, P, tex); m.wall(ROOM_X1, 0, 0, 0, P, tex);
  // [Doruk-Aksoy] The room's east wall, opened onto each corridor, walked north to south.
  let y = H;
  CLOSETS.slice().reverse().forEach((closet, ri) => {
    const yc = SPACING / 2 + (CLOSETS.length - 1 - ri) * SPACING;
    const C = m.sector(lit), D = m.sector({ ...lit, heightceiling: 0, id: TAG.door }), K = m.sector({ ...lit, id: TAG.closet });
    m.wall(ROOM_X1, y, ROOM_X1, yc + HALF_W, P, tex);
    m.opening(ROOM_X1, yc + HALF_W, ROOM_X1, yc - HALF_W, P, C);
    y = yc - HALF_W;
    m.wall(ROOM_X1, yc + HALF_W, DOOR_X, yc + HALF_W, C, tex); m.wall(DOOR_X, yc - HALF_W, ROOM_X1, yc - HALF_W, C, tex);
    m.opening(DOOR_X, yc + HALF_W, DOOR_X, yc - HALF_W, C, D, { texturetop: "BIGDOOR2" }, {});
    m.wall(DOOR_X, yc + HALF_W, CLOSET_X0, yc + HALF_W, D, { texturemiddle: "DOORTRAK" });
    m.wall(CLOSET_X0, yc - HALF_W, DOOR_X, yc - HALF_W, D, { texturemiddle: "DOORTRAK" });
    m.opening(CLOSET_X0, yc + HALF_W, CLOSET_X0, yc - HALF_W, D, K, {}, { texturetop: "BIGDOOR2" });
    const { x1, y0, y1 } = placeCloset(m, closet, yc);
    m.wall(CLOSET_X0, y0, CLOSET_X0, yc - HALF_W, K, tex); m.wall(CLOSET_X0, yc + HALF_W, CLOSET_X0, y1, K, tex);
    m.wall(CLOSET_X0, y1, x1, y1, K, tex); m.wall(x1, y1, x1, y0, K, tex); m.wall(x1, y0, CLOSET_X0, y0, K, tex);
  });
  m.wall(ROOM_X1, y, ROOM_X1, 0, P, tex);
  m.thing({ x: 256, y: H / 2, angle: 0, type: 1 });
  return m;
}

// [Doruk-Aksoy] The words of `ThingCountSector(T_NONE, tid, tag)`, left on the stack.
export const countIn = (tid, tag) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, tag, PCD.THINGCOUNTSECTOR];

// [Doruk-Aksoy] Puked by the player: every door opens, WAKE_TICS later the player's noise wakes every closet (the monsters face away, so sound alone), and COUNT_AFTER_TICS after that each closet's cvar is set to its monsters still in its closet or door.
export function closetsBehavior() {
  const count = CLOSETS.flatMap((closet, i) => [PCD.PUSHNUMBER, i, ...countIn(closet.tid, TAG.closet), ...countIn(closet.tid, TAG.door), PCD.ADD,
    PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP]);
  return acsScripts([
    { code: [...callSpecial(SPECIAL.Door_Open, [TAG.door, 64, 0]), PCD.DELAYDIRECT, WAKE_TICS, ...callSpecial(SPECIAL.NoiseAlert, [0, 0]),
      PCD.DELAYDIRECT, COUNT_AFTER_TICS, ...count, PCD.TERMINATE], open: false, number: RUN_SCRIPT },
  ], CLOSETS.map(cvarOf));
}

// [Doruk-Aksoy] The map, its scripts and one CVARINFO counter per closet.
export function buildClosetsWad() {
  const cvarinfo = CLOSETS.map((closet) => `server int ${cvarOf(closet)} = -1;\n`).join("");
  return wad([...mapLumps("MAP01", buildClosetsMap(), { behavior: closetsBehavior() }), { name: "CVARINFO", data: Buffer.from(cvarinfo, "latin1") }]);
}

// [Doruk-Aksoy] One counter per closet, read once the count has run; -1 means the count never ran.
export const COUNTERS = CLOSETS.map((closet) => ({ command: cvarOf(closet), label: `${closet.name} still in their closet after 30 s`,
  pattern: `"${cvarOf(closet)}" is "(-?\\d+)"`, ceiling: closet.left, floor: 0 }));

export const HOLD_TICS = WAKE_TICS + COUNT_AFTER_TICS + 35;
export const VANTAGES = [{ name: "room", at: { x: 256, y: (SPACING * CLOSETS.length) / 2, z: 0, angle: 0, pitch: 0 } }];
// [Doruk-Aksoy] Monsters on (offline scenarios launch without them), notarget toggled off (the launch turns on god and notarget, and a closet must hear the player), then the run's script.
export const SIDES = [{ name: "closets", launchCvars: { ...MUTE, sv_nomonsters: 0 }, commands: ["notarget", `puke ${RUN_SCRIPT}`] }];

runScenarioIfMain(import.meta.url, () => ({ name: "chaseclosets", wad: buildClosetsWad(), maps: ["MAP01"], vantages: VANTAGES,
  counter: COUNTERS, sides: SIDES, holdTics: HOLD_TICS }), compareCounter);

// [Doruk-Aksoy] ACS functions called with fewer arguments than they read, the stack past them full of other values: SpawnForced without its angle spawns at angle 0, a SpawnForced short of its position spawns nothing, and BCC's two-argument SpawnSpotForced spawns without a TID.
//
// usage:  node scenarios/acsargcount.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acs0, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] The script's strings: the class it spawns and the counter each check that holds adds to.
export const STRINGS = ["Clip", "zx_acsargs_passed"];
const CLIP = 0, PASSED = 1;
// [Doruk-Aksoy] Where the items appear, a closed room east of the player's that no vantage sees, and the map spot's TID there.
export const HIDDEN = { x: 1152, y: 256, spot: 60 };
const fixed = (v) => v * 65536;

// [Doruk-Aksoy] Six copies of `value` pushed and dropped, so the stack past the next call's arguments holds it, as a script's earlier work leaves it.
export function staleStack(value) {
  return [...Array.from({ length: 6 }, () => [PCD.PUSHNUMBER, value]).flat(), ...Array(6).fill(PCD.DROP)];
}

// [Doruk-Aksoy] The words of `SetCVar(strings[i], GetCVar(strings[i]) + bit)`, its result dropped.
function addBit(i, bit) {
  return [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, i, PCD.GETCVAR, PCD.PUSHNUMBER, bit, PCD.ADD, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP];
}

// [Doruk-Aksoy] The bit each check adds when it holds, and the one the script adds on reaching its end, so 15 is every check held and a lower number names those that failed.
export const BITS = { angle: 1, short: 2, spot: 4, ran: 8 };

// [Doruk-Aksoy] The three checks, each adding its bit to the counter when it holds, then the bit that says the script ran.
export function buildScript() {
  const s = new AcsScript();
  s.emit(PCD.DELAYDIRECT, 5);
  // [Doruk-Aksoy] SpawnForced(class, x, y, z, tid) spawns one item, at angle 0 rather than the 192 past its arguments.
  s.emit(...staleStack(192), ...callFunc(ACSF.SpawnForced, [CLIP, fixed(HIDDEN.x), fixed(HIDDEN.y), 0, 50]), PCD.PUSHNUMBER, 1, PCD.EQ);
  s.jump(PCD.IFNOTGOTO, "short");
  s.emit(PCD.PUSHNUMBER, 50, PCD.GETACTORANGLE, PCD.PUSHNUMBER, 0, PCD.EQ).jump(PCD.IFNOTGOTO, "short");
  s.emit(...addBit(PASSED, BITS.angle));
  // [Doruk-Aksoy] SpawnForced(class, x, y) is short of its z, so nothing spawns, where the 193s past it were its z, TID and angle.
  s.label("short").emit(...staleStack(193), ...callFunc(ACSF.SpawnForced, [CLIP, fixed(HIDDEN.x), fixed(HIDDEN.y)]), PCD.DROP);
  s.emit(PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 193, PCD.THINGCOUNT, PCD.PUSHNUMBER, 0, PCD.EQ).jump(PCD.IFNOTGOTO, "spot");
  s.emit(...addBit(PASSED, BITS.short));
  // [Doruk-Aksoy] SpawnSpotForced(class, spot), as BCC compiles it, spawns one item with no TID, which upstream's minimum of 4 would refuse.
  s.label("spot").emit(...staleStack(194), ...callFunc(ACSF.SpawnSpotForced, [CLIP, HIDDEN.spot]), PCD.PUSHNUMBER, 1, PCD.EQ);
  s.jump(PCD.IFNOTGOTO, "done");
  s.emit(PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 194, PCD.THINGCOUNT, PCD.PUSHNUMBER, 0, PCD.EQ).jump(PCD.IFNOTGOTO, "done");
  s.emit(...addBit(PASSED, BITS.spot));
  s.label("done").emit(...addBit(PASSED, BITS.ran), PCD.TERMINATE);
  return s.words();
}

export function buildAcsArgCountPk3() {
  const m = new UdmfMap();
  const walls = () => ({ side: { texturemiddle: "STARTAN2" } });
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, walls);
  m.room(1024, 0, 1280, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, walls);
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  m.thing({ x: HIDDEN.x, y: 384, angle: 0, type: THING.MapSpot, id: HIDDEN.spot });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acs0(buildScript(), STRINGS) })) },
    { name: "cvarinfo.txt", data: "server int zx_acsargs_passed = 0;\n" },
  ]);
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

export const COUNTER = { command: "zx_acsargs_passed", label: "checks that held (angle 1, short 2, spot 4, ran 8)",
  pattern: "\"zx_acsargs_passed\" is \"(\\d+)\"", floor: BITS.angle + BITS.short + BITS.spot + BITS.ran };

export const SIDES = [{ name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "acsargcount", wad: buildAcsArgCountPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);

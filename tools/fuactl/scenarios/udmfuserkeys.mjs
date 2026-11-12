// [rc4l] UDMF user_ keys on a sector, a line and a side, which zandronum@201cc5719f73 keeps to the ZDoom namespaces that define them; exit 0 when the "doom" namespace map reads them as unset and the "zdoom" one reads them back.
//
// usage:  node scenarios/udmfuserkeys.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, checksScript, checksCounter, ACSF } from "../src/acs.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

export const COUNTER_CVAR = "zx_udmfuserkeys_wrong";
export const KEY = "user_score";
const STRINGS = [COUNTER_CVAR, KEY];
const VALUE = 7;
// [rc4l] The room's tag and its west wall's ID, which every base namespace allows.
const TAG = 5, LINE_ID = 6;
const PLAYER = { x: 256, y: 128 };

// [rc4l] The words that leave the user key's value on the stack for the sector of a tag, the line of an ID, or that line's front side.
export const READ = {
  sector: (tag) => callFunc(ACSF.GetSectorUDMFInt, [tag, STRINGS.indexOf(KEY)]),
  line: (id) => callFunc(ACSF.GetLineUDMFInt, [id, STRINGS.indexOf(KEY)]),
  side: (id) => callFunc(ACSF.GetSideUDMFInt, [id, 0, STRINGS.indexOf(KEY)]),
};

const reads = (want) => [["the sector's", "sector", TAG, want], ["the line's", "line", LINE_ID, want], ["the side's", "side", LINE_ID, want]];
// [rc4l] Each map's namespace and its checks: the base "doom" namespace drops the keys, "zdoom" keeps them.
export const MAPS = { MAP01: { namespace: "doom", checks: reads(0) }, MAP02: { namespace: "zdoom", checks: reads(VALUE) } };

export function buildMap(namespace) {
  const m = new UdmfMap({ namespace });
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160,
    id: TAG, [KEY]: VALUE }, (edge) => ({ side: { texturemiddle: "STARTAN2", ...(edge === "west" ? { [KEY]: VALUE } : {}) },
    line: edge === "west" ? { id: LINE_ID, [KEY]: VALUE } : {} }));
  m.thing({ ...PLAYER, angle: 0, type: 1 });
  return m;
}

export function buildUserKeysPk3() {
  return pk3([
    ...Object.entries(MAPS).map(([map, { namespace, checks }]) => ({ name: `maps/${map}.wad`, data: wad(mapLumps(map, buildMap(namespace),
      { behavior: acsScripts([{ code: checksScript(checks, READ), open: false, number: 1 }], STRINGS) })) })),
    { name: "mapinfo.txt", data: Object.keys(MAPS).map((map) => `map ${map} "User keys ${MAPS[map].namespace}"\n{\n}\n`).join("") },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

export const COUNTER = checksCounter(COUNTER_CVAR, (map) => MAPS[map].checks);

runScenarioIfMain(import.meta.url, () => ({ name: "udmfuserkeys", wad: buildUserKeysPk3(), maps: Object.keys(MAPS),
  vantages: [{ name: "room", at: { ...PLAYER, z: 0, angle: 0, pitch: 0 } }], counter: COUNTER,
  sides: [{ name: "keys", launchCvars: { ...MUTE }, commands: ["puke 1"] }] }), compareCounter);

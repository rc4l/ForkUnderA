// [rc4l] A clientside script setting its player's user cvar (zandronum@02962ee6283e): exit 0 when the server learns the new value.
//
// usage:  node scenarios/netacsuserinfo.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, callFunc, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] The player's user cvar, and the server cvar the server copies what it holds of it into.
export const STRINGS = ["zx_acsuser", "zx_acsuser_seen"];
const USER = 0, SEEN = 1;
export const VALUE = 7;

// [rc4l] ENTER, clientside: SetUserCVar(PlayerNumber(), "zx_acsuser", 7).
export function clientScript() {
  return [PCD.PLAYERNUMBER, PCD.PUSHNUMBER, USER, PCD.PUSHNUMBER, VALUE, PCD.CALLFUNC, 3, ACSF.SetUserCVar, PCD.DROP, PCD.TERMINATE];
}

// [rc4l] OPEN, on the server: every five tics, SetCVar("zx_acsuser_seen", GetUserCVar(0, "zx_acsuser")), starting at word `at` of the lump.
export function serverScript(at) {
  const s = new AcsScript();
  s.label("loop").emit(PCD.PUSHNUMBER, SEEN, ...callFunc(ACSF.GetUserCVar, [0, USER]), PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP, PCD.DELAYDIRECT, 5);
  s.jump(PCD.GOTO, "loop");
  return s.words(at);
}

export function buildScripts() {
  const client = clientScript();
  return [{ code: client, number: 1, type: SCRIPT.ENTER, flags: SCRIPTF.CLIENTSIDE }, { code: serverScript(client.length), number: 2 }];
}

export function buildNetAcsUserInfoPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(buildScripts(), STRINGS) })) },
    { name: "cvarinfo.txt", data: `user int ${STRINGS[USER]} = 0;\nserver int ${STRINGS[SEEN]} = 0;\n` },
  ]);
}

export async function judgeNetplay(set, label) {
  await cap.waitTics(set.server.c, 70);
  const seen = Number(/is "(-?\d+)"/.exec(String(await cap.exec(set.server.c, STRINGS[SEEN])))?.[1]);
  const own = Number(/is "(-?\d+)"/.exec(String(await cap.exec(set.clients[0].c, STRINGS[USER])))?.[1]);
  const faults = [];
  if (own !== VALUE) faults.push(`the client's own ${STRINGS[USER]} is ${own}, not ${VALUE}`);
  if (seen !== VALUE) faults.push(`the server holds the player's ${STRINGS[USER]} as ${seen}, not ${VALUE}`);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the server holds ${VALUE}`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "clientside", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsuserinfo", wad: buildNetAcsUserInfoPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

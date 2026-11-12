// [rc4l] A clientside script setting its player's user cvar (zandronum@02962ee6283e), and a burst of them beside a menu change (zandronum@8cd5694e6ca6): exit 0 when the server learns each value and keeps the player.
//
// usage:  node scenarios/netacsuserinfo.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, callFunc, callSpecial, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] The player's user cvar, and the server cvar the server copies what it holds of it into.
export const STRINGS = ["zx_acsuser", "zx_acsuser_seen"];
const USER = 0, SEEN = 1;
export const VALUE = 7;
// [rc4l] The burst sets the cvar to BASE plus each of its BURST tics, past the 105-tic window the joining value counts in.
export const BASE = 100, BURST = 60, SETTLE_TICS = 110;
// [rc4l] Tics into the burst the player changes their gender, after its first two sends, inside the 42-tic window two back.
export const GENDER_AT = 20;

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

// [rc4l] Closed and clientside: SetUserCVar(ConsolePlayerNumber(), "zx_acsuser", BASE + i) on each of BURST tics, from word `at`.
export function burstScript(at) {
  const s = new AcsScript();
  s.emit(PCD.PUSHNUMBER, 0, PCD.ASSIGNSCRIPTVAR, 0);
  s.label("loop").emit(PCD.INCSCRIPTVAR, 0, PCD.CALLFUNC, 0, ACSF.ConsolePlayerNumber, PCD.PUSHNUMBER, USER, PCD.PUSHNUMBER, BASE, PCD.PUSHSCRIPTVAR, 0, PCD.ADD,
    PCD.CALLFUNC, 3, ACSF.SetUserCVar, PCD.DROP, PCD.DELAYDIRECT, 1, PCD.PUSHSCRIPTVAR, 0, PCD.PUSHNUMBER, BURST, PCD.LT);
  s.jump(PCD.IFGOTO, "loop").emit(PCD.TERMINATE);
  return s.words(at);
}

// [rc4l] Closed, on the server: ACS_ExecuteAlways(3), which the server hands to every client since script 3 is clientside.
export function startBurstScript() {
  return [...callSpecial(SPECIAL.ACS_ExecuteAlways, [3, 0]), PCD.TERMINATE];
}

export function buildScripts() {
  const client = clientScript();
  const server = serverScript(client.length);
  const burst = burstScript(client.length + server.length);
  return [{ code: client, number: 1, type: SCRIPT.ENTER, flags: SCRIPTF.CLIENTSIDE }, { code: server, number: 2 },
    { code: burst, number: 3, type: SCRIPT.CLOSED, flags: SCRIPTF.CLIENTSIDE }, { code: startBurstScript(), number: 4, type: SCRIPT.CLOSED }];
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

const cvarOf = async (c, name) => Number(/is "(-?\d+)"/.exec(String(await cap.exec(c, name)))?.[1]);

// [rc4l] What is wrong once the scripts ran: the client's own value and the server's copy of it must be `want`, and the player still on the server.
export function judgeValues({ own, seen, kept }, want) {
  if (!kept) return ["the server kicked the player"];
  const faults = [];
  if (own !== want) faults.push(`the client's own ${STRINGS[USER]} is ${own}, not ${want}`);
  if (seen !== want) faults.push(`the server holds the player's ${STRINGS[USER]} as ${seen}, not ${want}`);
  return faults;
}

export async function judgeNetplay(set, label, { config }) {
  const client = set.clients[0];
  await cap.waitTics(set.server.c, 70);
  let want = VALUE;
  if (config.name === "burst") {
    await cap.waitTics(set.server.c, SETTLE_TICS);
    await cap.exec(set.server.c, "puke 4");
    await cap.waitTics(set.server.c, GENDER_AT);
    await cap.exec(client.c, "gender female").catch(() => {});
    await cap.waitTics(set.server.c, BURST + 40);
    want = BASE + BURST;
  }
  const kept = ((await set.server.c.rpc("net.state", {}))?.clients || []).some((c) => c.player === client.player);
  // [rc4l] A kicked client sits in its full console, which leaves later checks without an answer.
  if (!kept) { client.c.close(); client.c = null; }
  const values = { kept, seen: await cvarOf(set.server.c, STRINGS[SEEN]), own: kept ? await cvarOf(client.c, STRINGS[USER]) : NaN };
  const faults = judgeValues(values, want);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the server holds ${want} and kept the player`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "clientside", players: 1 }, { name: "burst", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netacsuserinfo", wad: buildNetAcsUserInfoPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

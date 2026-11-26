// [Doruk-Aksoy] APROP_MaxStepHeight and APROP_MaxDropOffHeight set by a server script (features/acs-parity): exit 0 when the server, its player's client and a late joiner all hold the new heights on the player's pawn.
//
// usage:  node scenarios/netacsstepheight.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, callSpecial, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { onSnapshots } from "../src/netacs.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [Doruk-Aksoy] Each client's user cvars hold what it sees of the pawn, the server cvars what the server holds.
export const STRINGS = ["zx_step", "zx_drop", "zx_step_server", "zx_drop_server"];
const STEP = 0, DROP = 1, STEP_SERVER = 2, DROP_SERVER = 3;
const SET_ACTIVATOR_TO_PLAYER = 131;
const APROP = { MaxStepHeight: 44, MaxDropOffHeight: 45 };
export const WANT = { step: 40 * 65536, drop: 100 * 65536 };
export const DEFAULT = 24 * 65536;
export const SET_SCRIPT = 1, SHOW_SCRIPT = 2, RESHOW_SCRIPT = 3;

const toPlayer = [PCD.PUSHSCRIPTVAR, 0, PCD.CALLFUNC, 1, SET_ACTIVATOR_TO_PLAYER, PCD.DROP];
const getProp = (prop) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, prop, PCD.GETACTORPROPERTY];
const setProp = (prop, value) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, prop, PCD.PUSHNUMBER, value, PCD.SETACTORPROPERTY];
const showClients = callSpecial(SPECIAL.ACS_ExecuteAlways, [SHOW_SCRIPT, 0]);

// [Doruk-Aksoy] Closed, on the server, with the player's number: sets both heights on its pawn, keeps what the server then holds, and has every client show its own.
export function setScript() {
  return [...toPlayer, ...setProp(APROP.MaxStepHeight, WANT.step), ...setProp(APROP.MaxDropOffHeight, WANT.drop),
    PCD.PUSHNUMBER, STEP_SERVER, ...getProp(APROP.MaxStepHeight), PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP,
    PCD.PUSHNUMBER, DROP_SERVER, ...getProp(APROP.MaxDropOffHeight), PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP, ...showClients, PCD.TERMINATE];
}

// [Doruk-Aksoy] Closed and clientside, its activator the pawn: the client's own user cvars take the heights it holds.
export function showScript() {
  const keep = (cvar, prop) => [...callFunc(ACSF.ConsolePlayerNumber, []), PCD.PUSHNUMBER, cvar, ...getProp(prop), PCD.CALLFUNC, 3, ACSF.SetUserCVar, PCD.DROP];
  return [...keep(STEP, APROP.MaxStepHeight), ...keep(DROP, APROP.MaxDropOffHeight), PCD.TERMINATE];
}

// [Doruk-Aksoy] Closed, on the server, with the player's number: every client shows the pawn's heights again, the late joiner among them.
export function reshowScript() {
  return [...toPlayer, ...showClients, PCD.TERMINATE];
}

export function buildNetStepHeightPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  const scripts = [{ code: setScript(), number: SET_SCRIPT, type: SCRIPT.CLOSED, args: 1 },
    { code: showScript(), number: SHOW_SCRIPT, type: SCRIPT.CLOSED, flags: SCRIPTF.CLIENTSIDE },
    { code: reshowScript(), number: RESHOW_SCRIPT, type: SCRIPT.CLOSED, args: 1 }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS, { enhanced: true }) })) },
    { name: "cvarinfo.txt", data: `user int ${STRINGS[STEP]} = 0;\nuser int ${STRINGS[DROP]} = 0;\nserver int ${STRINGS[STEP_SERVER]} = 0;\nserver int ${STRINGS[DROP_SERVER]} = 0;\n` },
  ]);
}

const cvarOf = async (c, name) => Number(/is "(-?\d+)"/.exec(String(await cap.exec(c, name)))?.[1]);

// [Doruk-Aksoy] What is wrong with the heights each machine holds, `held` as { who: { step, drop } }, the server's first.
export function judgeHeights(held) {
  return Object.entries(held).flatMap(([who, h]) => [
    ...(h.step === WANT.step ? [] : [`${who} holds a step height of ${h.step / 65536}, not ${WANT.step / 65536}`]),
    ...(h.drop === WANT.drop ? [] : [`${who} holds a drop-off height of ${h.drop / 65536}, not ${WANT.drop / 65536}`]),
  ]);
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [player] = set.clients;
  await cap.waitTics(sc, 35);
  await cap.exec(sc, `puke ${SET_SCRIPT} ${player.player}`);
  await cap.waitTics(sc, 35);
  const late = await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35);
  await cap.exec(sc, `puke ${RESHOW_SCRIPT} ${player.player}`);
  await cap.waitTics(sc, 35);
  const read = async (c, step, drop) => ({ step: await cvarOf(c, STRINGS[step]), drop: await cvarOf(c, STRINGS[drop]) });
  const faults = judgeHeights({ "the server": await read(sc, STEP_SERVER, DROP_SERVER), "the player's client": await read(player.c, STEP, DROP),
    "the late joiner": await read(late.c, STEP, DROP) });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the server, the player's client and a late joiner hold a step height of 40 and a drop-off height of 100`, failed: faults.length ? 1 : 0 };
}

// [Doruk-Aksoy] --side lan --cvar fua_debug_netdrop=65 leaves SetThingProperty unsent, and both clients keep the default 24.
export const CONFIGS = onSnapshots([{ name: "lan", players: 1 }, { name: "ping100", players: 1, lag: "100" }]);

runScenarioIfMain(import.meta.url, () => ({ name: "netacsstepheight", wad: buildNetStepHeightPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

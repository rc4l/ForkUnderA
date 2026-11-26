// [Doruk-Aksoy] ACS actor handles in a CLIENTSIDE script (features/acs-handles): exit 0 when a server script sends a barrel's net id, and each client, a late
// joiner too, makes it its activator, takes a handle from its own table that resolves to that barrel, while the server holds a handle of its own.
//
// usage:  node scenarios/netacshandles.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";
import { FN, BARREL as HANDLES_BARREL, buildAcsHandlesPk3, judgeFirst, stepValues, logScript, global, keep } from "./acshandles.mjs";
import { withNetCopies, addPlayerStarts, netJudge, onSnapshots } from "../src/netacs.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [Doruk-Aksoy] Each client's user cvars hold what its script found; the server cvar holds the server's own handle.
export const STRINGS = ["zx_h", "zx_hok", "zx_hbarrel", "zx_hserver", "ExplosiveBarrel"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
const BARREL = 5, GET_NET_ID = 215, SET_ACTIVATOR_BY_NET_ID = 216;
export const SEND_SCRIPT = 1, CLIENT_SCRIPT = 2;

const fn = (name, ...args) => callFunc(FN[name], args);
const userCvar = (cvar, words) => [...callFunc(ACSF.ConsolePlayerNumber, []), PCD.PUSHNUMBER, cvar, ...words, PCD.CALLFUNC, 3, ACSF.SetUserCVar, PCD.DROP];

// [Doruk-Aksoy] Closed, on the server: keeps its own handle to the barrel, then runs the clientside script on every client with the barrel's net id.
export function sendScript() {
  return [PCD.PUSHNUMBER, S.zx_hserver, ...fn("GetActorHandle", BARREL), PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP,
    PCD.PUSHNUMBER, CLIENT_SCRIPT, PCD.PUSHNUMBER, 0, ...callFunc(GET_NET_ID, [BARREL, 0]), PCD.LSPEC1 + 2, SPECIAL.ACS_ExecuteAlways, PCD.TERMINATE];
}

// [Doruk-Aksoy] Clientside, its argument a net id: that actor becomes the activator, and the client's own handle to it is kept in its user cvars.
export function clientScript() {
  return [PCD.PUSHSCRIPTVAR, 0, PCD.CALLFUNC, 1, SET_ACTIVATOR_BY_NET_ID, PCD.DROP,
    ...fn("GetActorHandle", 0), PCD.ASSIGNSCRIPTVAR, 1,
    ...userCvar(S.zx_h, [PCD.PUSHSCRIPTVAR, 1]),
    ...userCvar(S.zx_hok, fn("HandleValid", [PCD.PUSHSCRIPTVAR, 1])),
    ...fn("SetActivatorHandle", [PCD.PUSHSCRIPTVAR, 1]), PCD.DROP,
    ...userCvar(S.zx_hbarrel, callFunc(ACSF.CheckActorClass, [0, S.ExplosiveBarrel])), PCD.TERMINATE];
}

export function buildNetHandlesPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  m.thing({ x: 512, y: 300, angle: 0, type: THING.ExplosiveBarrel, id: BARREL });
  const scripts = [{ code: sendScript(), number: SEND_SCRIPT, type: SCRIPT.CLOSED },
    { code: clientScript(), number: CLIENT_SCRIPT, type: SCRIPT.CLOSED, flags: SCRIPTF.CLIENTSIDE, args: 1 }];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS, { enhanced: true }) })) },
    { name: "cvarinfo.txt", data: `user int zx_h = 0;\nuser int zx_hok = 0;\nuser int zx_hbarrel = 0;\nserver int zx_hserver = 0;\n` },
  ]);
}

const cvarOf = async (c, name) => Number(/is "(-?\d+)"/.exec(String(await cap.exec(c, name)))?.[1]);

// [Doruk-Aksoy] What is wrong with what each machine holds: the server a handle, each client a valid handle to the barrel.
export function judgeClients(server, clients) {
  const faults = server > 0 ? [] : [`the server's own handle is ${server}`];
  for (const [who, c] of Object.entries(clients))
    if (!(c.h > 0 && c.ok === 1 && c.barrel === 1)) faults.push(`${who} took handle ${c.h}, valid ${c.ok}, barrel ${c.barrel}, not a valid handle to the barrel`);
  return faults;
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [player] = set.clients;
  await cap.waitTics(sc, 35);
  await cap.exec(sc, `puke ${SEND_SCRIPT}`);
  await cap.waitTics(sc, 35);
  const late = await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35);
  await cap.exec(sc, `puke ${SEND_SCRIPT}`);
  await cap.waitTics(sc, 35);
  const read = async (c) => ({ h: await cvarOf(c, "zx_h"), ok: await cvarOf(c, "zx_hok"), barrel: await cvarOf(c, "zx_hbarrel") });
  const faults = judgeClients(await cvarOf(sc, "zx_hserver"), { "the player's client": await read(player.c), "the late joiner": await read(late.c) });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the player's client and a late joiner each took a handle to the barrel from its net id, and the server holds its own`, failed: faults.length ? 1 : 0 };
}

// [Doruk-Aksoy] acshandles' first step on every machine, then a barrel the clients hold handles to that the server removes: 6 keeps a handle on each client,
// 7 removes the barrel on the server alone, and 8 reads each client's kept handle and the tid again, both stale once the removal reached it.
export const COPY_STEPS = [{ script: 1, tags: ["h1"] }, { script: 6, tags: ["h6"], server: false }, { script: 7, tags: ["h7"], clients: false }, { script: 8, tags: ["h8"], server: false }];
const OWN = {
  6: logScript("h6", [global(1), fn("HandleValid", global(1))], keep(1, fn("GetActorHandle", HANDLES_BARREL.removed))),
  7: logScript("h7", [[PCD.PUSHNUMBER, 1]], [PCD.PUSHNUMBER, HANDLES_BARREL.removed, PCD.LSPEC1, SPECIAL.Thing_Remove]),
  8: logScript("h8", [fn("HandleValid", global(1)), fn("GetActorHandle", HANDLES_BARREL.removed)]),
};
export const STARTS = [[64, 64], [448, 64], [448, 448]];

export function buildCopiesPk3() {
  return buildAcsHandlesPk3({
    withScripts: (s) => withNetCopies([...s, ...Object.entries(OWN).map(([n, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(n) }))], [1, 6, 7, 8]),
    withMap: (m) => addPlayerStarts(m, STARTS),
  });
}

// [Doruk-Aksoy] What is wrong with one machine's copies run: its own handles from script 1, and on a client that saw it, the kept handle stale after the removal.
export function judgeCopies(logs, member) {
  const { faults } = judgeFirst(logs);
  if (member.role === "server") {
    if (!/h7 1/.test(String(logs.h7))) faults.push("script 7 never removed the barrel");
  } else if (member.role !== "latejoin") {
    const [kept, valid] = stepValues(logs, "h6", faults);
    if (!(kept > 0 && valid === 1)) faults.push(`its handle to the second barrel was ${kept}, valid ${valid}`);
    const [after, again] = stepValues(logs, "h8", faults);
    if (after !== 0 || again !== 0) faults.push(`after the server removed the barrel its handle read valid ${after} and the tid gave handle ${again}, not 0 and 0`);
  }
  return faults;
}

const judgeCopiesRun = netJudge({ steps: COPY_STEPS, late: COPY_STEPS.slice(0, 1), judge: judgeCopies,
  passed: "every machine's handles resolve, follow pointers and set the activator from its own table, and a barrel the server removed left each client's handle stale" });

// [Doruk-Aksoy] The net id configs take a handle from a server's net id; the copies configs run acshandles' steps on every machine.
export const CONFIGS = onSnapshots([{ name: "lan", players: 1 }, { name: "ping100", players: 1, lag: "100" },
  { name: "copies-lan", players: 1, observers: 1, copies: true, wad: buildCopiesPk3() },
  { name: "copies-ping100", players: 1, observers: 1, lag: "100,100", copies: true, wad: buildCopiesPk3() }]);

export const judgeEither = (set, label, ctx) => (ctx?.config?.copies ? judgeCopiesRun(set, label) : judgeNetplay(set, label));

runScenarioIfMain(import.meta.url, () => ({ name: "netacshandles", wad: buildNetHandlesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay: judgeEither }), compareNetplay);

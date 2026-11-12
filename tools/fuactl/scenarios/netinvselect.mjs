// [rc4l] ACS's SetSelectedInventory in a netgame (zandronum@6107a231590b): exit 0 when a server script selecting a player's second inventory-bar item moves the selection on the server and on that player's client.
//
// usage:  node scenarios/netinvselect.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] Two inventory-bar items: giving the first selects it on both sides, so only SetSelectedInventory moves the selection to the second.
export const FIRST = "ZxCharmA", SECOND = "ZxCharmB";
export const DECORATE = [FIRST, SECOND].map((name) => `actor ${name} : Inventory\n{\n  Inventory.MaxAmount 1\n  Inventory.Icon "ARM1A0"\n  +INVENTORY.INVBAR\n}\n`).join("");
// [rc4l] The log prefixes come first, so a call the binary lacks, returning 0, prints a prefix rather than an item.
export const STRINGS = ["server ", "client ", FIRST, SECOND];
const SERVER = 0, CLIENT = 1, FIRST_AT = 2, SECOND_AT = 3;
export const GIVE = 1, READ = 2, SELECT = 3;

// [rc4l] Log the prefix at `at` and the activator's selected item.
const logSelected = (at) => [PCD.BEGINPRINT, PCD.PUSHNUMBER, at, PCD.PRINTSTRING, ...callFunc(ACSF.GetSelectedInventory, []), PCD.PRINTSTRING, PCD.ENDLOG];

// [rc4l] Puked by the client: GIVE and SELECT run on the server, giving both items and then selecting the second with the bar shown; READ logs the client's own selection.
export function buildScripts() {
  const give = (at) => [PCD.PUSHNUMBER, at, PCD.PUSHNUMBER, 1, PCD.GIVEINVENTORY];
  return [
    { number: GIVE, type: SCRIPT.CLOSED, flags: SCRIPTF.NET, code: [...give(FIRST_AT), ...give(SECOND_AT), ...logSelected(SERVER), PCD.TERMINATE] },
    { number: READ, type: SCRIPT.CLOSED, flags: SCRIPTF.NET | SCRIPTF.CLIENTSIDE, code: [...logSelected(CLIENT), PCD.TERMINATE] },
    { number: SELECT, type: SCRIPT.CLOSED, flags: SCRIPTF.NET, code: [...callFunc(ACSF.SetSelectedInventory, [SECOND_AT, 1]), PCD.DROP, ...logSelected(SERVER), PCD.TERMINATE] },
  ];
}

export function buildNetInvSelectPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  const behavior = acsScripts(buildScripts(), STRINGS);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] The item a log line names after `prefix`, "" for none, or null when no such line was printed.
export function selectedIn(log, prefix) {
  const m = new RegExp(`^${prefix} (\\S*)\\s*$`, "m").exec(String(log));
  return m ? m[1] : null;
}

// [rc4l] What is wrong with the selections the server and the client logged once the items were given and once the second was selected.
export function judgeSelections({ given, selected }) {
  const faults = [];
  for (const [when, logged, want] of [["once both were given", given, FIRST], ["once the server selected the second", selected, SECOND]]) {
    for (const who of ["server", "client"]) {
      const got = logged[who];
      if (got === null) faults.push(`the ${who} logged no selection ${when}`);
      else if (got !== want) faults.push(`the ${who} had ${got ? got : "nothing"} selected ${when}, not ${want}`);
    }
  }
  return faults;
}

// [rc4l] The server's selection after the client pukes `script`, sent again while the client is still joining and the server runs nothing.
async function serverRuns(sc, cc, script) {
  for (let tries = 0; tries < 5; tries++) {
    const heard = cap.listen(sc);
    try {
      await cap.exec(cc, `puke ${script}`, { quietMs: 60 });
      await cap.waitTics(sc, 35);
    } finally { heard.off(); }
    const got = selectedIn(heard.text(), "server");
    if (got !== null) return got;
  }
  return null;
}

const clientReads = async (cc) => selectedIn(await cap.exec(cc, `puke ${READ}`, { quietMs: 300 }), "client");

export async function judgeNetplay(set, label) {
  const sc = set.server.c, cc = set.clients[0].c;
  const given = { server: await serverRuns(sc, cc, GIVE) };
  given.client = await clientReads(cc);
  const selected = { server: await serverRuns(sc, cc, SELECT) };
  selected.client = await clientReads(cc);
  const faults = judgeSelections({ given, selected });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the selection moved from ${FIRST} to ${SECOND} on the server and the client`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "lan", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netinvselect", wad: buildNetInvSelectPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

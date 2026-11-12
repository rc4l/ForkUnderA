// [rc4l] A pickup spawned on a raised floor during a netgame, then respawning (zandronum@4c658ffc6887): exit 0 when the client puts the respawned pickup on the floor where the server does, not a floor's height above it to fall back down.
//
// usage:  node scenarios/netpickupheight.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] A step a player walks up, and the pickup standing on it.
export const STEP = 24;
export const PICKUP = "ZxBonus", PICKUP_NUM = 31200;
// [rc4l] A health bonus that comes back a second after it is taken, in any game, and that a script's spawn leaves undropped as a mod's placed pickup is.
export const DECORATE = `actor ${PICKUP} : HealthBonus ${PICKUP_NUM}\n{\n  Inventory.RespawnTics 35\n  +INVENTORY.ALWAYSRESPAWN\n  States\n  {\n  Spawn:\n    BON1 A 0 A_ChangeFlag("DROPPED", 0)\n    BON1 ABCDCB 6\n    Loop\n  }\n}\n`;
// [rc4l] Where script 1 spawns the pickup, on the step; a spawn during play reaches the client without the spawn point a joiner is sent.
export const AT = { x: 512, y: 256 };

// [rc4l] Script 1 spawns the pickup on the step.
export function buildScript() {
  const fixed = (n) => n * 65536;
  return [...callFunc(ACSF.SpawnForced, [0, fixed(AT.x), fixed(AT.y), fixed(STEP), 0, 0]), PCD.DROP, PCD.TERMINATE];
}

export function buildNetPickupHeightPk3() {
  const m = new UdmfMap();
  const floor = m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(384, 160, 640, 352, { ...ROOM, heightfloor: STEP }, floor, () => ({ texturebottom: "STARTAN2" }));
  // [rc4l] The player runs north up the step and over the pickup.
  m.thing({ x: 512, y: 64, angle: 90, type: 1 });
  const behavior = acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], [PICKUP]);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) }, { name: "decorate.txt", data: DECORATE }]);
}

// [rc4l] The pickup's height on one instance, or null when it has none.
async function pickupZ(c) {
  const row = ((await c.rpc("world.actors", {}))?.actors || []).find((a) => a.c === PICKUP);
  return row ? row.z : null;
}

// [rc4l] The server's health for the client's player, which the bonus raises past 100.
async function playerHealth(c) {
  const row = ((await c.rpc("world.actors", { players: true }))?.actors || []).find((a) => a.player >= 0);
  return row ? row.health : null;
}

// [rc4l] What is wrong with the heights read before the pickup was taken and after it came back, `health` the player's once it ran over it, `highest` the client's highest each tic between.
export function judgeHeights({ before, after, health, highest }) {
  if (!(health > 100)) return [`the player never took the pickup, health ${health}`];
  const faults = [];
  if (before.server !== STEP || before.client !== STEP) faults.push(`before it was taken the pickup stood at ${before.server} on the server and ${before.client} on the client, not ${STEP}`);
  if (after.server !== STEP) faults.push(`the server respawned the pickup at ${after.server}, not ${STEP}`);
  else if (after.client !== after.server) faults.push(`the client respawned the pickup at ${after.client}, the server at ${after.server}`);
  if (highest > STEP) faults.push(`the client respawned the pickup as high as ${highest} before it fell to the step at ${STEP}`);
  return faults;
}

// [rc4l] The pickup's highest height on the client across `samples` reads, one a tic under the game thread.
async function highestZ(c, samples) {
  let highest = -Infinity;
  for (let i = 0; i < samples; i++) highest = Math.max(highest, (await pickupZ(c)) ?? -Infinity);
  return highest;
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c, cc = set.clients[0].c;
  await cap.exec(sc, "puke 1");
  await cap.waitTics(sc, 10);
  const before = { server: await pickupZ(sc), client: await pickupZ(cc) };
  await cap.exec(cc, "+forward", { quietMs: 60 });
  await cap.waitTics(sc, 40);
  await cap.exec(cc, "-forward", { quietMs: 60 });
  const health = await playerHealth(sc);
  // [rc4l] The pickup's second hidden, its respawn sequence, and the command's way to the client.
  const highest = await highestZ(cc, 105);
  const after = { server: await pickupZ(sc), client: await pickupZ(cc) };
  const faults = judgeHeights({ before, after, health, highest });
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the pickup respawned at ${STEP} on both`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "lan", players: 1, clientCvars: { cl_run: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netpickupheight", wad: buildNetPickupHeightPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

// [rc4l] A hosted room where one player walks over two items, crosses a line whose script relights and recolours a sector and changes a wall, and fires a rocket, while an observer watches: exit 0 when the observer sees every one of them as the server has it, on loopback and at 100 ms of ping, catching what a server sends only to the player who caused it.
//
// usage:  node scenarios/netbroadcast.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";
import { netdiff, diffLines } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

const LOOK_TAG = 30, WALL_ID = 31;
const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
// [rc4l] Clips, which a player at full health still picks up, where a stimpack stays on the floor.
const ITEMS = /^Clip$/;

// [rc4l] What the line's script leaves.
export const AFTER = { light: 224, color: 0x40c0ff, wallTex: "SW1BRCOM" };

// [rc4l] One long room: the player's start facing north over two clips toward a strip whose south edge runs script 2 when crossed, the observer's start beside it, and a dim sector the script changes.
export function buildNetBroadcastWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1536, { ...flats, lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(16, 448, 1008, 456, { ...flats, lightlevel: 192 }, room, () => ({}), () => ({}),
    (edge) => (edge === "south" ? { special: SPECIAL.ACS_Execute, arg0: 2, playercross: true } : {}));
  m.block(700, 1100, 900, 1300, { ...flats, lightlevel: 96, id: LOOK_TAG }, room, () => ({}), () => ({ texturemiddle: "STARTAN2" }),
    (edge) => (edge === "south" ? { id: WALL_ID } : {}));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  m.thing({ x: 256, y: 128, angle: 90, type: 2 });
  m.thing({ x: 512, y: 224, type: THING.Clip });
  m.thing({ x: 512, y: 320, type: THING.Clip });
  const change = [
    ...callSpecial(SPECIAL.Light_ChangeToValue, [LOOK_TAG, AFTER.light]),
    ...callSpecial(SPECIAL.Sector_SetColor, [LOOK_TAG, 0x40, 0xc0, 0xff, 0]),
    PCD.PUSHNUMBER, WALL_ID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 0, PCD.SETLINETEXTURE,
    PCD.TERMINATE];
  return wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: change, open: false, number: 2 }], [AFTER.wallTex]) }));
}

const actorsOf = async (c) => (await c.rpc("world.actors", {}))?.actors || [];

// [rc4l] The verdict from what was seen: the items each side holds before and after the walk, whether the observer saw the rocket fly, what changed on the server and the netdiffs.
export function judgeBroadcast(label, { itemsBefore, itemsAfterServer, itemsAfterObserver, rocketSeen, serverChanged, diffs }) {
  const faults = [];
  if (itemsBefore < 2) faults.push(`the observer held ${itemsBefore} items before the walk, not 2`);
  if (itemsAfterServer !== 0) faults.push(`the player picked up ${2 - itemsAfterServer} of 2 items, so the walk proves nothing`);
  if (itemsAfterObserver !== 0) faults.push(`the observer still holds ${itemsAfterObserver} item(s) the player picked up`);
  if (!rocketSeen) faults.push("the observer never saw the player's rocket");
  if (!serverChanged) faults.push("the line's script never changed the server's map");
  for (const d of diffs) if (d.diffs.length) faults.push(`${d.who}: ${diffLines("", d.diffs, d.compared).slice(1).join(";").trim()}`);
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the observer saw both pickups, the rocket and the script's changes as the server has them`, failed: 0 };
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [player, observer] = set.clients;
  for (const c of set.clients) await cap.exec(c.c, "god", { quietMs: 120 });
  await cap.waitTics(sc, 35);
  const itemsBefore = (await actorsOf(observer.c)).filter((a) => ITEMS.test(a.c)).length;
  await cap.exec(player.c, "+forward", { quietMs: 40 });
  await cap.sleep(2500);
  await cap.exec(player.c, "-forward", { quietMs: 40 });
  await cap.waitTics(sc, 35);
  const itemsAfterServer = (await actorsOf(sc)).filter((a) => ITEMS.test(a.c)).length;
  const itemsAfterObserver = (await actorsOf(observer.c)).filter((a) => ITEMS.test(a.c)).length;
  await cap.exec(player.c, "give RocketLauncher");
  await cap.exec(player.c, "use RocketLauncher");
  await cap.waitTics(sc, 70);
  await cap.exec(player.c, "+attack", { quietMs: 40 });
  let rocketSeen = false;
  const until = Date.now() + 2500;
  while (!rocketSeen && Date.now() < until) {
    rocketSeen = (await actorsOf(observer.c)).some((a) => /rocket/i.test(a.c) && !/launcher/i.test(a.c));
    if (!rocketSeen) await cap.sleep(40);
  }
  await cap.exec(player.c, "-attack", { quietMs: 40 });
  await cap.waitTics(sc, 70);
  const { readWorld } = await import("../src/netdiff.mjs");
  const world = await readWorld(sc);
  const serverChanged = world.sectors.some((s) => s.light === AFTER.light && s.color === AFTER.color) && world.sides.some((s) => s.mid === AFTER.wallTex);
  const diffs = [];
  for (const c of set.clients) diffs.push({ who: `${c.role} player ${c.player}`, ...(await netdiff(sc, c.c, { seconds: 1 })) });
  return judgeBroadcast(label, { itemsBefore, itemsAfterServer, itemsAfterObserver, rocketSeen, serverChanged, diffs });
}

// [rc4l] On loopback and at 100 ms of ping; --side NAME --cvar fua_debug_netdrop=ID shows the observer missing what the server stopped sending.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netbroadcast", wad: buildNetBroadcastWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

// [rc4l] A hosted map where the server opens a door, lowers a lift and changes a sector's light, colour, floor and wall texture while a player kills a monster and an observer watches, then a third player joins late: exit 0 when the server, the player, the observer and the late joiner all hold the same world, clean, at 100 ms of ping and with the observer losing a tenth of its packets.
//
// usage:  node scenarios/netsync.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";
import { addClient } from "../src/netplay.mjs";
import { netdiff, diffLines } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

const DOOR_TAG = 10, LIFT_TAG = 11, LOOK_TAG = 12, WALL_ID = 20, ZOMBIE_TID = 5;
const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };
const plain = { side: { texturemiddle: "STARTAN2" } };

// [rc4l] What the server's script leaves, which every client must come to hold.
export const AFTER = { doorCeilingAbove: 200, liftFloor: 0, light: 240, color: 0xff8040, floorTex: "FLAT14", wallTex: "SW1BRCOM" };

// [rc4l] One room: three starts facing east at a zombieman, a closed door, a raised lift and a dim sector whose look script 2 changes.
export function buildNetSyncWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 768, { ...flats, lightlevel: 192 }, () => plain);
  m.block(448, 600, 576, 632, { ...flats, heightceiling: 0, lightlevel: 192, id: DOOR_TAG }, room, () => ({ texturemiddle: "-", texturetop: "BIGDOOR2" }));
  m.block(800, 100, 960, 260, { ...flats, heightfloor: 64, lightlevel: 192, id: LIFT_TAG }, room, () => ({ texturemiddle: "-", texturebottom: "PLAT1" }));
  m.block(160, 560, 360, 700, { ...flats, lightlevel: 96, id: LOOK_TAG }, room, () => ({}), () => ({ texturemiddle: "STARTAN2" }), (edge) => (edge === "south" ? { id: WALL_ID } : {}));
  for (const [i, y] of [[1, 384], [2, 256], [3, 512]]) m.thing({ x: 128, y, angle: 0, type: i });
  m.thing({ x: 320, y: 384, angle: 180, type: THING.Zombieman, id: ZOMBIE_TID });
  const change = [
    ...callSpecial(SPECIAL.Door_Open, [DOOR_TAG, 64]),
    ...callSpecial(SPECIAL.Floor_LowerByValue, [LIFT_TAG, 32, 64]),
    ...callSpecial(SPECIAL.Light_ChangeToValue, [LOOK_TAG, AFTER.light]),
    ...callSpecial(SPECIAL.Sector_SetColor, [LOOK_TAG, 0xff, 0x80, 0x40, 0]),
    PCD.PUSHNUMBER, WALL_ID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 1, PCD.SETLINETEXTURE,
    PCD.CHANGEFLOORDIRECT, LOOK_TAG, 0,
    PCD.TERMINATE];
  const behavior = acsScripts([{ code: change, open: false, number: 2 }], [AFTER.floorTex, AFTER.wallTex]);
  return wad(mapLumps("MAP01", m, { behavior }));
}

// [rc4l] What is wrong with the server's own world after the script and the kill, so agreement is about a world that changed.
export function serverChanged({ sectors, sides, actors }) {
  const faults = [];
  const door = sectors.find((s) => s.ceiling > AFTER.doorCeilingAbove && s.floor === 0 && s.ceilingTex === "CEIL3_5" && s.i !== 0 && s.light === 192);
  if (!door) faults.push("no door opened");
  if (!sectors.some((s) => s.light === AFTER.light && s.color === AFTER.color && s.floorTex === AFTER.floorTex)) faults.push("the dim sector's light, colour or floor never changed");
  if (!sides.some((s) => s.mid === AFTER.wallTex)) faults.push("the wall texture never changed");
  if (sectors.some((s) => s.floor === 64)) faults.push("the lift never lowered");
  const zombie = actors.find((a) => /zombie/i.test(a.c));
  if (zombie && zombie.health > 0) faults.push(`the zombieman lives (${zombie.health} health)`);
  return faults;
}

// [rc4l] Fire held until the server's zombieman is dead, or the time runs out.
async function killZombie(pc, sc, { seconds = 8 } = {}) {
  await cap.exec(pc, "+attack", { quietMs: 60 });
  const until = Date.now() + seconds * 1000;
  try {
    while (Date.now() < until) {
      const z = ((await sc.rpc("world.actors", {}))?.actors || []).find((a) => /zombie/i.test(a.c));
      if (!z || z.health <= 0) return;
      await cap.sleep(200);
    }
  } finally {
    await cap.exec(pc, "-attack", { quietMs: 60 });
  }
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const [player] = set.clients;
  for (const c of set.clients) await cap.exec(c.c, "god", { quietMs: 120 });
  await cap.waitTics(sc, 35);
  await killZombie(player.c, sc);
  await cap.exec(sc, "puke 2");
  await cap.waitTics(sc, 35 * 4);
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35 * 2);
  const { readWorld } = await import("../src/netdiff.mjs");
  const faults = serverChanged(await readWorld(sc));
  const lines = [];
  let failed = 0;
  if (faults.length) { lines.push(`FAIL ${label}: the server's world did not change as scripted: ${faults.join("; ")}`); failed++; }
  for (const c of set.clients) {
    const r = await netdiff(sc, c.c, { seconds: 1 });
    const out = diffLines(`${label} ${c.role} player ${c.player}`, r.diffs, r.compared);
    lines.push(...out.map((l) => l.replace(/^AGREE/, "PASS").replace(/^DIFFER/, "FAIL")));
    if (r.diffs.length) failed++;
  }
  return { lines, failed };
}

// [rc4l] On loopback, at 100 ms of ping for every client, and with the observer losing a tenth of its packets; --side NAME --cvar fua_debug_netdrop=ID shows the clients missing what the server stopped sending.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
  { name: "lossy-observer", players: 1, observers: 1, loss: "0,10" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netsync", wad: buildNetSyncWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

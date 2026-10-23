// [rc4l] A hosted map whose server script changes everything only a server writes of a sector and a line (gravity, colour, fade, friction, rotation, scale, reflection, floor, blocking, translucency, texture): exit 0 when the player, an observer and a late joiner hold all of it as the server does, at 100 ms of ping and with the observer losing a tenth of its packets, and the server sent every change the tic it happened.
//
// usage:  node scenarios/netmapstate.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, callSpecial, PCD } from "../src/acs.mjs";
import { addClient } from "../src/netplay.mjs";
import { clientAgreement, readWorld } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";

const LOOK_TAG = 40, LINE_ID = 41;
const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5" };

// [rc4l] What script 2 leaves: the line blocks monsters, projectiles and hitscans (Line_SetBlocking's bits 2, 16 and 512), which clients were never told before.
export const AFTER = { gravity: 0.5, color: 0x20a0ff, fade: 0x101030, rotation: [45, 90], floorTex: "FLAT14", wallTex: "SW1BRCOM",
  lineFlags: 0x00000002 | 0x01000000 | 0x08000000 };

// [rc4l] One room: three starts and a block whose sector script 2 changes and whose south wall, two-sided, it changes too.
export function buildNetMapStateWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 768, { ...flats, lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(500, 400, 760, 640, { ...flats, lightlevel: 160, id: LOOK_TAG }, room, () => ({}), () => ({ texturemiddle: "STARTAN2" }),
    (edge) => (edge === "south" ? { id: LINE_ID } : {}));
  for (const [i, y] of [[1, 384], [2, 256], [3, 512]]) m.thing({ x: 128, y, angle: 0, type: i });
  const change = [
    ...callSpecial(SPECIAL.Sector_SetGravity, [LOOK_TAG, 0, 50]),
    ...callSpecial(SPECIAL.Sector_SetColor, [LOOK_TAG, 0x20, 0xa0, 0xff, 0]),
    ...callSpecial(SPECIAL.Sector_SetFade, [LOOK_TAG, 0x10, 0x10, 0x30]),
    ...callSpecial(SPECIAL.Sector_SetFriction, [LOOK_TAG, 200]),
    ...callSpecial(SPECIAL.Sector_SetRotation, [LOOK_TAG, AFTER.rotation[0], AFTER.rotation[1]]),
    ...callSpecial(SPECIAL.Sector_SetFloorScale2, [LOOK_TAG, 2 * 65536, 2 * 65536]),
    ...callSpecial(SPECIAL.Sector_SetPlaneReflection, [LOOK_TAG, 64, 0]),
    PCD.CHANGEFLOORDIRECT, LOOK_TAG, 0,
    ...callSpecial(SPECIAL.Line_SetBlocking, [LINE_ID, 2 | 16 | 512, 0]),
    ...callSpecial(SPECIAL.TranslucentLine, [LINE_ID, 128]),
    PCD.PUSHNUMBER, LINE_ID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 1, PCD.SETLINETEXTURE,
    PCD.TERMINATE];
  return wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: change, open: false, number: 2 }], [AFTER.floorTex, AFTER.wallTex]) }));
}

// [rc4l] What is wrong with the server's own world after the script, so agreement is about a world that changed.
export function serverChanged({ sectors, sides, lines }) {
  const faults = [];
  const look = sectors.find((s) => s.light === 160);
  if (!look) return ["the changed sector is not there"];
  if (look.gravity !== AFTER.gravity) faults.push(`gravity ${look.gravity}`);
  if (look.color !== AFTER.color) faults.push(`colour ${look.color}`);
  if (look.fade !== AFTER.fade) faults.push(`fade ${look.fade}`);
  if (Math.abs(look.friction - 0.90625) < 0.001) faults.push("friction never changed");
  if (String(look.rotation) !== String(AFTER.rotation)) faults.push(`rotation ${look.rotation}`);
  if (look.scale?.[0] === 1 && look.scale?.[1] === 1) faults.push("floor scale never changed");
  if (!(look.reflect?.[0] > 0)) faults.push("floor reflection never changed");
  if (look.floorTex !== AFTER.floorTex) faults.push(`floor ${look.floorTex}`);
  if (!sides.some((s) => s.mid === AFTER.wallTex)) faults.push("the wall texture never changed");
  if (!lines.some((l) => (l.flags & AFTER.lineFlags) === AFTER.lineFlags && l.alpha < 0.75)) faults.push("the line's blocking or translucency never changed");
  return faults;
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  for (const c of set.clients) await cap.exec(c.c, "god", { quietMs: 120 });
  await cap.waitTics(sc, 35);
  await cap.exec(sc, "puke 2");
  await cap.waitTics(sc, 35 * 2);
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35 * 2);
  const faults = serverChanged(await readWorld(sc));
  const agreed = await clientAgreement(sc, set.clients, label);
  if (!faults.length) return agreed;
  return { lines: [`FAIL ${label}: the server's map did not change as scripted: ${faults.join("; ")}`, ...agreed.lines], failed: agreed.failed + 1 };
}

// [rc4l] On loopback, at 100 ms of ping and with the observer losing a tenth of its packets; --side lan --cvar fua_debug_netdrop=ID (a SetSector* or SetLine* id from netcap) shows the clients missing what was not sent.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
  { name: "lossy-observer", players: 1, observers: 1, loss: "0,10" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netmapstate", wad: buildNetMapStateWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

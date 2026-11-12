// [rc4l] Another player's height and speed as a client hears them (zandronum@9c7fa2cfd010): exit 0 when an observer holds a slow, hovering player's speed and height as the server does, where both came rounded down to whole units.
//
// usage:  node scenarios/netmoveprecision.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

// [rc4l] A push of half a unit a tic and a height half a unit past a whole one, both below what a whole-unit field holds.
export const FLOOR = 8, HOVER = 32.5, PUSH = 0.5;
const ROOM = { heightfloor: FLOOR, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] How far the observer's readings may stray from the server's.
export const TOLERANCE = 0.1;

// [rc4l] ENTER, on the server: every tic, SetActorPosition(0, GetActorX(0), GetActorY(0), FLOOR + HOVER, false) and SetActorVelocity(0, PUSH, 0, 0, false, false).
export function pushScript() {
  const s = new AcsScript();
  s.label("loop").emit(PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.GETACTORX, PCD.PUSHNUMBER, 0, PCD.GETACTORY, PCD.PUSHNUMBER, (FLOOR + HOVER) * 65536, PCD.PUSHNUMBER, 0, PCD.SETACTORPOSITION, PCD.DROP,
    ...callFunc(ACSF.SetActorVelocity, [0, PUSH * 65536, 0, 0, 0, 0]), PCD.DROP, PCD.DELAYDIRECT, 1);
  s.jump(PCD.GOTO, "loop");
  return s.words(0);
}

export function buildNetMovePrecisionPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 2048, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 128]]) m.thing({ x, y: type * 128, angle: 0, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: pushScript(), type: SCRIPT.ENTER, number: 1 }], []) })) }]);
}

// [rc4l] What is wrong with the observer's reading of the pushed player: its speed and height must be the server's, the speed not rounded away.
export function judgeReadings(server, observer) {
  if (!server || !observer) return [`the pushed player is missing on the ${server ? "observer" : "server"}`];
  const faults = [];
  if (!(server.vx > TOLERANCE)) faults.push(`the server's player moves at ${server.vx}, not pushed`);
  if (Math.abs(observer.vx - server.vx) > TOLERANCE) faults.push(`the observer has the player moving at ${observer.vx}, the server ${server.vx}`);
  if (Math.abs(observer.z - server.z) > TOLERANCE) faults.push(`the observer has the player at height ${observer.z}, the server ${server.z}`);
  return faults;
}

const pawnOf = async (c, player) => ((await c.rpc("world.actors", { players: true }))?.actors || []).find((a) => a.player === player) ?? null;

export async function judgeNetplay(set, label) {
  const mover = set.clients.find((c) => c.role === "player");
  const observer = set.clients.find((c) => c.role === "observer");
  await cap.waitTics(set.server.c, 70);
  const faults = judgeReadings(await pawnOf(set.server.c, mover.player), await pawnOf(observer.c, mover.player));
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the observer holds the player's speed and height`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "slow", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netmoveprecision", wad: buildNetMovePrecisionPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

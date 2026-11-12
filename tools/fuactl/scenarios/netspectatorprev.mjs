// [rc4l] A client's interpolation while it spectates (zandronum@318c98576461): exit 0 when a spectating client that flew and stopped has its pawn's previous position where it now is, not left where it began spectating.
//
// usage:  node scenarios/netspectatorprev.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] How far the previous position may lag once the spectator has stopped: a fraction of a unit, where a frozen one is the whole flight.
export const STOPPED_LAG = 1;
export const FLIGHT_TICS = 40;

export function buildNetSpectatorPrevPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 2048, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] Starts at the west end, facing east down the long room.
  for (const [type, y] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x: 128, y, angle: 0, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] The client's own pawn, which only it has while it spectates.
async function ownPawn(c) {
  return ((await c.rpc("world.actors", { players: true, all: true }))?.actors || []).find((a) => a.player >= 0) || null;
}

// [rc4l] What is wrong with the pawn read once it began spectating and once it flew and stopped.
export function judgePrev(start, stopped) {
  if (!start || !stopped) return ["the spectating client has no pawn of its own"];
  if (typeof stopped.px !== "number") return ["this build's world.actors reports no previous position"];
  const flown = Math.hypot(stopped.x - start.x, stopped.y - start.y);
  if (flown < 64) return [`the spectator flew only ${flown.toFixed(1)} units`];
  const lag = Math.hypot(stopped.x - stopped.px, stopped.y - stopped.py);
  return lag <= STOPPED_LAG ? [] : [`the stopped spectator's previous position is ${lag.toFixed(1)} units behind it, after a flight of ${flown.toFixed(1)}`];
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c, cc = set.clients[0].c;
  await cap.exec(cc, "spectate");
  await cap.waitTics(sc, 35);
  const start = await ownPawn(cc);
  await cap.exec(cc, "+forward", { quietMs: 60 });
  await cap.waitTics(sc, FLIGHT_TICS);
  await cap.exec(cc, "-forward", { quietMs: 60 });
  // [rc4l] A spectator's flight slows to a stop within a second.
  await cap.waitTics(sc, 70);
  const faults = judgePrev(start, await ownPawn(cc));
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the stopped spectator's previous position kept up with it`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "lan", players: 1, clientCvars: { cl_run: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netspectatorprev", wad: buildNetSpectatorPrevPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

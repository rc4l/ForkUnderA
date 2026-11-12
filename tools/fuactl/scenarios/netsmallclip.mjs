// [rc4l] A light pebble a server drops into a TERRAIN liquid whose small splash sinks 12 units (zandronum@7e0d5383e6ab), hosted with a player and an observer: exit 0 when each client holds the splash at the server's floorclip.
//
// usage:  node scenarios/netsmallclip.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const POOL = { heightfloor: 0, heightceiling: 256, texturefloor: "FWATER1", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const DROP_SPOT = 10;
export const SMALL_CLIP = 12;

// [rc4l] A liquid whose small splash, for anything under 10 mass, sinks SMALL_CLIP units and never goes away.
export const TERRAIN = `splash ZXClipped
{
	smallclass ZXSmallSplash
	smallclip ${SMALL_CLIP}
	baseclass None
	chunkclass None
}
terrain ZXClippedWater
{
	splash ZXClipped
	liquid
}
floor FWATER1 ZXClippedWater
`;

export const DECORATE = `
Actor ZXSmallSplash
{
  +NOINTERACTION
  States
  {
  Spawn:
    BAL1 A -1
    Stop
  }
}

Actor ZXPebble
{
  Radius 4
  Height 4
  Mass 5
  States
  {
  Spawn:
    BAL1 A -1
    Stop
  }
}
`;

// [rc4l] A 1024x512 pool holding the starts, and the drop spot 128 units above its middle.
export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, POOL, () => ({ side: WALL }));
  m.thing({ x: 512, y: 256, height: 128, angle: 0, type: THING.MapSpot, id: DROP_SPOT });
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

// [rc4l] Script 1, puked on the server, drops the pebble.
export function behavior() {
  const code = [...callFunc(ACSF.SpawnSpotForced, [0, DROP_SPOT, 0, 0]), PCD.DROP, PCD.TERMINATE];
  return acsScripts([{ code, open: false, number: 1 }], ["ZXPebble"], { enhanced: true });
}

export function buildNetSmallClipPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap(), { behavior: behavior() })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "terrain.txt", data: TERRAIN },
    { name: "mapinfo.txt", data: `map MAP01 "Clipped pool"\n{\n}\n` },
  ]);
}

// [rc4l] What is wrong with an instance's splashes: none, no clip reported, or a clip other than SMALL_CLIP.
export function judgeSplash(actors) {
  const splashes = (actors || []).filter((a) => a.c === "ZXSmallSplash");
  if (!splashes.length) return ["holds no small splash"];
  const faults = [];
  for (const s of splashes) {
    if (typeof s.clip !== "number") faults.push(`splash ${s.netid} reports no floorclip`);
    else if (Math.abs(s.clip - SMALL_CLIP) > 0.01) faults.push(`splash ${s.netid} sinks ${s.clip} units, not ${SMALL_CLIP}`);
  }
  return faults;
}

// [rc4l] Long enough for the pebble to fall 128 units and its splash to reach every client.
export const SETTLE_TICS = 105;

const actorsOf = async (c) => (await c.rpc("world.actors", { limit: 100000 }))?.actors || [];

export async function judgeNetplay(set) {
  const sc = set.server.c;
  await cap.exec(sc, "puke 1");
  await cap.waitTics(sc, SETTLE_TICS);
  const lines = [];
  let failed = 0;
  const report = (name, faults) => {
    lines.push(faults.length ? `FAIL ${name}: ${faults.join("; ")}` : `PASS ${name}: its splash sinks ${SMALL_CLIP} units`);
    if (faults.length) failed++;
  };
  report("server", judgeSplash(await actorsOf(sc)));
  for (const c of set.clients) report(`${c.role} player ${c.player}`, judgeSplash(await actorsOf(c.c)));
  return { lines, failed };
}

export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netsmallclip", wad: buildNetSmallClipPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

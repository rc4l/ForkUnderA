// [rc4l] A player whose class number is past a byte (zandronum@9cee7e80ec24, @51c804da8b73): exit 0 when the server and every client spawn the player as the class they chose, not one 256 below it.
//
// usage:  node scenarios/netclasses.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
// [rc4l] More classes than a byte numbers, the chosen one far enough past 255 that a byte would make it another class.
export const CLASS_COUNT = 300, CHOSEN = 299;
export const className = (i) => `ZXClass${i}`;

// [rc4l] Every class a plain DoomPlayer, named by its display name as the playerclass cvar matches it.
export function buildDecorate() {
  return Array.from({ length: CLASS_COUNT }, (_, i) => `actor ${className(i)} : DoomPlayer { Player.DisplayName "${className(i)}" }\n`).join("");
}

export function buildKeyconf() {
  return ["clearplayerclasses", ...Array.from({ length: CLASS_COUNT }, (_, i) => `addplayerclass ${className(i)}`)].join("\n") + "\n";
}

export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: WALL }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

export function buildNetClassesPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap())) },
    { name: "decorate.txt", data: buildDecorate() },
    { name: "keyconf.txt", data: buildKeyconf() },
    { name: "mapinfo.txt", data: `map MAP01 "Class room"\n{\n}\n` },
  ]);
}

// [rc4l] What is wrong with the class an instance spawned the player's body as.
export function judgeClass(actors, player, want = className(CHOSEN)) {
  const body = (actors || []).find((a) => a.player === player);
  if (!body) return [`holds no body for player ${player}`];
  return body.c === want ? [] : [`spawned player ${player} as ${body.c}, not ${want}`];
}

export async function judgeNetplay(set, label) {
  const lines = [];
  let failed = 0;
  const chooser = set.clients.find((c) => c.role === "player");
  await cap.waitTics(set.server.c, 35);
  for (const [role, inst] of [["server", set.server], ...set.clients.map((c) => [`${c.role} player ${c.player}`, c])]) {
    const actors = (await inst.c.rpc("world.actors", { players: true }))?.actors;
    const faults = judgeClass(actors, chooser.player);
    const name = `${label} ${role}`;
    lines.push(faults.length ? `FAIL ${name}: ${faults.join("; ")}` : `PASS ${name}: spawned player ${chooser.player} as ${className(CHOSEN)}`);
    if (faults.length) failed++;
  }
  return { lines, failed };
}

export const CONFIGS = [{ name: "chosen", players: 1, observers: 1, clientCvars: { playerclass: className(CHOSEN) } }];

runScenarioIfMain(import.meta.url, () => ({ name: "netclasses", wad: buildNetClassesPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

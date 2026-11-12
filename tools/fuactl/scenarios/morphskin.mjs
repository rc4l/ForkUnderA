// [rc4l] A player morphed into a class without NOSKIN (zandronum@3f8adfdae9db): exit 0 when the server and every client, one joining after the morph too, show the morph in its own sprite, not the base skin of the class it came from.
//
// usage:  node scenarios/morphskin.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MORPH = "ZxMorph", MORPH_SPRITE = "TROO";
// [rc4l] A player class with the imp's sprite and no NOSKIN, which a skin may still dress.
export const DECORATE = `actor ${MORPH} : PlayerPawn\n{\n  Player.DisplayName "${MORPH}"\n  States\n  {\n  Spawn:\n    ${MORPH_SPRITE} A -1\n    Stop\n  See:\n    ${MORPH_SPRITE} AB 4\n    Loop\n  }\n}\n`;

export function buildMorphSkinPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: `map MAP01 "Morph room"\n{\n}\n` },
  ]);
}

// [rc4l] What is wrong with how an instance shows the morphed player: as the morph's class, in the morph's sprite.
export function judgeMorph(actors, player) {
  const body = (actors || []).find((a) => a.player === player);
  if (!body) return [`holds no body for player ${player}`];
  if (body.c !== MORPH) return [`shows player ${player} as ${body.c}, not ${MORPH}`];
  return body.sprite.startsWith(MORPH_SPRITE) ? [] : [`shows the morphed player ${player} as ${body.sprite}, not ${MORPH_SPRITE}`];
}

// [rc4l] Long enough for the morph to reach the server and every client.
export const SETTLE_TICS = 35;

export async function judgeNetplay(set, label) {
  const morphed = set.clients.find((c) => c.role === "player");
  await cap.exec(morphed.c, `morphme ${MORPH}`);
  await cap.waitTics(set.server.c, SETTLE_TICS);
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(set.server.c, SETTLE_TICS);
  const lines = [];
  let failed = 0;
  for (const [name, inst] of [["server", set.server], ...set.clients.map((c) => [`${c.role} player ${c.player}`, c])]) {
    const faults = judgeMorph((await inst.c.rpc("world.actors", { players: true }))?.actors, morphed.player);
    lines.push(faults.length ? `FAIL ${label} ${name}: ${faults.join("; ")}` : `PASS ${label} ${name}: shows player ${morphed.player} as ${MORPH} in ${MORPH_SPRITE}`);
    if (faults.length) failed++;
  }
  return { lines, failed };
}

export const CONFIGS = [{ name: "morphed", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "morphskin", wad: buildMorphSkinPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

// [rc4l] A player morphed into a class without NOSKIN (zandronum@3f8adfdae9db): exit 0 when the server and every client, one joining after the morph too, show the morph in its own sprite, not the base skin of the class it came from.
// [Doruk-Aksoy] Then the morph is undone and every instance shows the player's own class again, its snapshot mirror included.
//
// usage:  node scenarios/morphskin.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const MORPH = "ZxMorph", MORPH_SPRITE = "TROO";
export const BASE = "DoomPlayer", BASE_SPRITE = "PLAY";
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

// [rc4l] What is wrong with how an instance shows the player: as the class wanted, in its sprite.
export function judgeBody(actors, player, cls, sprite) {
  const bodies = (actors || []).filter((a) => a.player === player);
  if (!bodies.length) return [`holds no body for player ${player}`];
  // [Doruk-Aksoy] A snapshot client lists a mirror only where no legacy body holds its id, so two rows mean the player's id is held by a body it no longer has.
  if (bodies.length > 1) return [`holds ${bodies.length} bodies for player ${player}: ${bodies.map((b) => b.c).join(", ")}`];
  const [body] = bodies;
  if (body.c !== cls) return [`shows player ${player} as ${body.c}, not ${cls}`];
  return body.sprite.startsWith(sprite) ? [] : [`shows player ${player} as ${body.sprite}, not ${sprite}`];
}

export const judgeMorph = (actors, player) => judgeBody(actors, player, MORPH, MORPH_SPRITE);

// [rc4l] Long enough for the morph to reach the server and every client.
export const SETTLE_TICS = 35;

export async function judgeNetplay(set, label) {
  const morphed = set.clients.find((c) => c.role === "player");
  const lines = [];
  let failed = 0;
  // [Doruk-Aksoy] Every instance's view of the player, judged against one class and sprite.
  const judgeAll = async (when, cls, sprite) => {
    for (const [name, inst] of [["server", set.server], ...set.clients.map((c) => [`${c.role} player ${c.player}`, c])]) {
      const faults = judgeBody((await inst.c.rpc("world.actors", { players: true }))?.actors, morphed.player, cls, sprite);
      lines.push(faults.length ? `FAIL ${label} ${when} ${name}: ${faults.join("; ")}` : `PASS ${label} ${when} ${name}: shows player ${morphed.player} as ${cls} in ${sprite}`);
      if (faults.length) failed++;
    }
  };
  await cap.exec(morphed.c, `morphme ${MORPH}`);
  await cap.waitTics(set.server.c, SETTLE_TICS);
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(set.server.c, SETTLE_TICS);
  await judgeAll("morphed", MORPH, MORPH_SPRITE);
  // [Doruk-Aksoy] The same morph again undoes it.
  await cap.exec(morphed.c, `morphme ${MORPH}`);
  await cap.waitTics(set.server.c, SETTLE_TICS);
  await judgeAll("unmorphed", BASE, BASE_SPRITE);
  return { lines, failed };
}

export const CONFIGS = [{ name: "morphed", players: 1, observers: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "morphskin", wad: buildMorphSkinPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

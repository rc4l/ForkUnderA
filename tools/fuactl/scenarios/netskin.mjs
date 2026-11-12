// [rc4l] A player takes a skin in a hosted game (PLAYER_SetSpriteToSkin, zandronum@4ab1128e5b73 and @ed337e36b307): exit 0 when every client shows the player's body in the skin's sprite, and in the base sprite where cl_skins is 0.
//
// usage:  node scenarios/netskin.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
export const SKIN = "zxskin", SKIN_SPRITE = "ZXSK";

const solid = (rgb, n = 32) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab: [n / 2, n] }); };

// [rc4l] A skin needs frames past A, so it gets A and B, each for every angle.
export const S_SKIN = `name = ${SKIN}\nsprite = ${SKIN_SPRITE}\n`;

export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: WALL }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

export function buildNetSkinPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap())) },
    { name: "S_SKIN", data: S_SKIN },
    ...["A0", "B0"].map((f) => ({ name: `sprites/${SKIN_SPRITE}${f}.png`, data: solid([0, 200, 255]) })),
    { name: "mapinfo.txt", data: `map MAP01 "Skin room"\n{\n}\n` },
  ]);
}

// [rc4l] What is wrong with the sprite an instance shows the skinned player's body in.
export function judgeSprite(actors, player, want) {
  const body = (actors || []).find((a) => a.player === player);
  if (!body) return [`holds no body for player ${player}`];
  return body.sprite.startsWith(want) ? [] : [`shows player ${player} as ${body.sprite}, not ${want}`];
}

// [rc4l] Long enough for a skin's userinfo to reach the server and every client.
export const SETTLE_TICS = 35;

export async function judgeNetplay(set, label, { config }) {
  const lines = [];
  let failed = 0;
  const skinned = set.clients.find((c) => c.role === "player");
  await cap.exec(skinned.c, `skin ${SKIN}`);
  await cap.waitTics(set.server.c, SETTLE_TICS);
  const want = config.clientCvars?.cl_skins === 0 ? "PLAY" : SKIN_SPRITE;
  for (const client of set.clients) {
    const actors = (await client.c.rpc("world.actors", { players: true }))?.actors;
    const faults = judgeSprite(actors, skinned.player, want);
    const name = `${label} ${client.role} player ${client.player}`;
    lines.push(faults.length ? `FAIL ${name}: ${faults.join("; ")}` : `PASS ${name}: shows player ${skinned.player} as ${want}`);
    if (faults.length) failed++;
  }
  return { lines, failed };
}

export const CONFIGS = [
  { name: "skins", players: 1, observers: 1 },
  { name: "noskins", players: 1, observers: 1, clientCvars: { cl_skins: 0 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netskin", wad: buildNetSkinPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

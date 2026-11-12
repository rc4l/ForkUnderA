// [rc4l] A player takes a skin in a hosted game (PLAYER_SetSpriteToSkin, zandronum@4ab1128e5b73 and @ed337e36b307): exit 0 when every client shows the player's body in the skin's sprite, and in the base sprite where cl_skins is 0; a skin only the player's class may wear reaches a client joining after them (zandronum@1a5a49be3bdc).
//
// usage:  node scenarios/netskin.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { solidPNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { addClient } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
export const SKIN = "zxskin", SKIN_SPRITE = "ZXSK";
// [rc4l] A skin for the second class alone, which a client resolving it against the first class drops.
export const CLASS_SKIN = "zxtwoskin", CLASS_SKIN_SPRITE = "ZXTW", CLASSES = ["ZxOne", "ZxTwo"];

// [rc4l] A skin needs frames past A, so it gets A and B, each for every angle.
export const S_SKIN = `name = ${SKIN}\nsprite = ${SKIN_SPRITE}\n`;
export const CLASS_S_SKIN = `name = ${CLASS_SKIN}\nsprite = ${CLASS_SKIN_SPRITE}\nclass = ${CLASSES[1]}\n`;
// [rc4l] Two plain DoomPlayers, named by their display names as the playerclass cvar and a skin's class match them.
export const DECORATE = CLASSES.map((c) => `actor ${c} : DoomPlayer { Player.DisplayName "${c}" }\n`).join("");
export const KEYCONF = ["clearplayerclasses", ...CLASSES.map((c) => `addplayerclass ${c}`)].join("\n") + "\n";

export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: WALL }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

// [rc4l] The class's skin in a wad of its own inside the pk3, as a skin wad carries its frames beside its S_SKIN, since a file holds one S_SKIN.
export function buildClassSkinWad() {
  return wad([{ name: "S_SKIN", data: CLASS_S_SKIN }, ...["A0", "B0"].map((f) => ({ name: `${CLASS_SKIN_SPRITE}${f}`, data: solidPNG([255, 200, 0]) }))]);
}

export function buildNetSkinPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap())) },
    { name: "S_SKIN", data: S_SKIN },
    ...["A0", "B0"].map((f) => ({ name: `sprites/${SKIN_SPRITE}${f}.png`, data: solidPNG([0, 200, 255]) })),
    { name: "zxtwoskin.wad", data: buildClassSkinWad() },
    { name: "decorate.txt", data: DECORATE },
    { name: "keyconf.txt", data: KEYCONF },
    { name: "mapinfo.txt", data: `map MAP01 "Skin room"\n{\n}\n` },
  ]);
}

// [rc4l] What is wrong with the sprite an instance shows the skinned player's body in.
export function judgeSprite(actors, player, want) {
  const body = (actors || []).find((a) => a.player === player);
  if (!body) return [`holds no body for player ${player}`];
  return body.sprite.startsWith(want) ? [] : [`shows player ${player} as ${body.sprite}, not ${want}`];
}

// [rc4l] The skin a configuration's player takes in the game.
export const skinOf = (config) => config.skin || SKIN;

// [rc4l] The sprite a configuration's skinned player should show: none where cl_skins is 0, else their skin's.
export function wantedSprite(config) {
  if (config.clientCvars?.cl_skins === 0) return "PLAY";
  return skinOf(config) === CLASS_SKIN ? CLASS_SKIN_SPRITE : SKIN_SPRITE;
}

// [rc4l] Long enough for a skin's userinfo to reach the server and every client.
export const SETTLE_TICS = 35;

export async function judgeNetplay(set, label, { config }) {
  const lines = [];
  let failed = 0;
  const skinned = set.clients.find((c) => c.role === "player");
  await cap.exec(skinned.c, `skin ${skinOf(config)}`);
  await cap.waitTics(set.server.c, SETTLE_TICS);
  if (config.lateJoin) {
    await addClient(set, { role: "latejoin" });
    await cap.waitTics(set.server.c, SETTLE_TICS);
  }
  const want = wantedSprite(config);
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
  // [rc4l] A skin only the player's class may wear, which a client joining after learns from the full update a joining client gets.
  { name: "classskin", players: 1, observers: 1, lateJoin: true, clientCvars: { playerclass: CLASSES[1] }, skin: CLASS_SKIN },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netskin", wad: buildNetSkinPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

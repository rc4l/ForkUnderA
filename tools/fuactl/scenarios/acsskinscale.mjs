// [rc4l] A skin given by ACS's SetPlayerSkin to a player standing still (zandronum@e97ab4a28f1a): exit 0 when their body is drawn at the skin's scale, where it kept its own until a state changed.
//
// usage:  node scenarios/acsskinscale.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { acsScripts, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const SKIN = "zxbig", SKIN_SPRITE = "ZXBG", SKIN_SCALE = 2;
// [rc4l] ACS's SetPlayerSkin, which acs.mjs has no name for yet.
export const SET_PLAYER_SKIN = 175;

const solid = (rgb, n = 32) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab: [n / 2, n] }); };

// [rc4l] Script 1: SetPlayerSkin(0, "zxbig"), which changes no state of the player's.
export function buildScript() {
  return [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.CALLFUNC, 2, SET_PLAYER_SKIN, PCD.DROP, PCD.TERMINATE];
}

export function buildAcsSkinScalePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED }], [SKIN]) })) },
    { name: "S_SKIN", data: `name = ${SKIN}\nsprite = ${SKIN_SPRITE}\nscale = ${SKIN_SCALE}\n` },
    ...["A0", "B0"].map((f) => ({ name: `sprites/${SKIN_SPRITE}${f}.png`, data: solid([0, 200, 255]) })),
  ]);
}

// [rc4l] What is wrong with the scale player 0's body is drawn at, before the skin (want 1) or after it (want the skin's).
export function judgeDrawnScale(actors, want) {
  const pawn = (actors || []).find((a) => a.player === 0);
  if (!pawn) return ["player 0 has no body"];
  if (Math.abs(pawn.dsx - want) > 0.01 || Math.abs(pawn.dsy - want) > 0.01) return [`player 0 is drawn at ${pawn.dsx} by ${pawn.dsy}, not ${want}`];
  return [];
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  const faults = judgeDrawnScale((await c.rpc("world.actors", { players: true }))?.actors, 1).map((f) => `before the skin ${f}`);
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 35);
  faults.push(...judgeDrawnScale((await c.rpc("world.actors", { players: true }))?.actors, SKIN_SCALE).map((f) => `with the skin ${f}`));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: drawn at 1, then at the skin's ${SKIN_SCALE} standing still`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "acsskinscale", wad: buildAcsSkinScalePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

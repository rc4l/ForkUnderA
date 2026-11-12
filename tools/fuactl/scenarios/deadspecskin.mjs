// [rc4l] A skinned player turned into a dead spectator in an offline survival game (zandronum@0e35b4f5c5d4): exit 0 when the body they leave keeps the skin's scale once, where it was scaled a second time.
//
// usage:  node scenarios/deadspecskin.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { acsScripts, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const SKIN = "zxbig", SKIN_SPRITE = "ZXBG", SKIN_SCALE = 2;
// [rc4l] ACS's SetDeadSpectator, which acs.mjs has no name for yet.
export const SET_DEAD_SPECTATOR = 130;

const solid = (rgb, n = 32) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab: [n / 2, n] }); };

// [rc4l] Script 1: SetDeadSpectator(0, true), the offline player turned into a dead spectator where they stand.
export function buildScript() {
  return [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.CALLFUNC, 2, SET_DEAD_SPECTATOR, PCD.DROP, PCD.TERMINATE];
}

export function buildDeadSpecSkinPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED }]) })) },
    { name: "S_SKIN", data: `name = ${SKIN}\nsprite = ${SKIN_SPRITE}\nscale = ${SKIN_SCALE}\n` },
    ...["A0", "B0"].map((f) => ({ name: `sprites/${SKIN_SPRITE}${f}.png`, data: solid([0, 200, 255]) })),
  ]);
}

// [rc4l] What is wrong with the body left behind: none, or a scale other than the skin's once.
export function judgeBody(actors) {
  const bodies = (actors || []).filter((a) => a.c === "DoomPlayer" && a.player < 0);
  if (bodies.length !== 1) return [`left ${bodies.length} bodies, not one`];
  const { sx, sy } = bodies[0];
  if (Math.abs(sx - SKIN_SCALE) > 0.01 || Math.abs(sy - SKIN_SCALE) > 0.01) return [`the body is scaled ${sx} by ${sy}, not the skin's ${SKIN_SCALE}`];
  return [];
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 105);
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 35);
  const r = await c.rpc("world.actors", { all: true });
  const faults = judgeBody(r?.actors);
  const spectator = (r?.actors || []).some((a) => a.c === "DoomPlayer" && a.player === 0);
  if (!spectator) faults.push("player 0 has no body of their own as a dead spectator");
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the body kept the skin's scale of ${SKIN_SCALE}`, failed: faults.length ? 1 : 0 };
}

// [rc4l] Survival offline with a one-second countdown, the one game an offline player becomes a dead spectator in, wearing the scaled skin.
export const SIDES = [{ name: "vk", launchCvars: { survival: 1, sv_survivalcountdowntime: 1, skin: SKIN } }];

runScenarioIfMain(import.meta.url, () => ({ name: "deadspecskin", wad: buildDeadSpecSkinPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

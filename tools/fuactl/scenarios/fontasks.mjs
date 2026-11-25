// [MGOOOOOO] A script on the game thread setting a font nothing provides, every tic, asks the main thread about it once: exit 0 when fua_gamethread_report counts at most MAX_ASKS calls over TICS tics of it (a HUD drawing an icon a class lacks did this, a frame's wait each tic).
//
// usage:  node scenarios/fontasks.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, AcsScript, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { judgeAsks, readAsks } from "../src/asks.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [MGOOOOOO] A name no lump, texture or font has.
export const NO_FONT = "ZXNOFONT";
export const LOOP_SCRIPT = 1;
export const TICS = 70;
// [MGOOOOOO] The first SetFont asks the main thread, which finds nothing; every later one is answered in the tic.
export const MAX_ASKS = 1;

// [MGOOOOOO] The puked script: SetFont(NO_FONT), wait a tic, again, for good.
export function loopScript() {
  return new AcsScript().label("top").emit(PCD.PUSHNUMBER, 0, PCD.SETFONT, PCD.DELAYDIRECT, 1).jump(PCD.GOTO, "top").words();
}

export function buildFontAsksPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 512, angle: 0, type: 1 });
  const scripts = [{ code: loopScript(), type: SCRIPT.CLOSED, number: LOOP_SCRIPT }];
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, [NO_FONT]) })) }]);
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 35);
  const before = await readAsks(cap, c);
  await cap.exec(c, `puke ${LOOP_SCRIPT}`);
  await cap.waitTics(c, TICS);
  const after = await readAsks(cap, c);
  const faults = judgeAsks(before, after, MAX_ASKS);
  return faults.length ? { line: `FAIL ${label}: SetFont("${NO_FONT}") every tic ${faults.join("; ")} in ${TICS} tics`, failed: 1 }
    : { line: `PASS ${label}: SetFont("${NO_FONT}") every tic for ${TICS} tics asked the main thread ${after - before} times (at most ${MAX_ASKS})`, failed: 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "fontasks", wad: buildFontAsksPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

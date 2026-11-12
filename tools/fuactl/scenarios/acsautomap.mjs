// [rc4l] ACS's CheckAutomap (zandronum@6869793234d6): exit 0 when it reads off, overlay, full and off again as togglemap steps through them under am_overlay 1.
//
// usage:  node scenarios/acsautomap.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] CheckAutomap's answers.
export const AUTOMAP = { off: 0, full: 1, overlay: 2 };
// [rc4l] What each togglemap leaves, from none: am_overlay 1 overlays first, then fills the screen, then closes.
export const STEPS = [{ toggles: 0, want: "off" }, { toggles: 1, want: "overlay" }, { toggles: 2, want: "full" }, { toggles: 3, want: "off" }];

// [rc4l] Script 1 logs "automap" and CheckAutomap's answer.
export function buildScript() {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING, ...callFunc(ACSF.CheckAutomap, []), PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE];
}

export function buildAcsAutomapPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], ["automap "]) })) }]);
}

// [rc4l] What is wrong with script 1's log after `toggles` togglemaps against the state wanted.
export function judgeStep(log, { toggles, want }) {
  const m = /automap (-?\d+)/.exec(String(log));
  if (!m) return [`after ${toggles} togglemaps script 1 logged nothing (${String(log).trim().slice(0, 80)})`];
  return Number(m[1]) === AUTOMAP[want] ? [] : [`after ${toggles} togglemaps CheckAutomap read ${m[1]}, not ${want}'s ${AUTOMAP[want]}`];
}

export const SIDES = [{ name: "vk", launchCvars: { am_overlay: 1 } }];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = [];
  for (const step of STEPS) {
    if (step.toggles > 0) await cap.exec(c, "togglemap");
    await cap.waitTics(c, 3);
    faults.push(...judgeStep(await cap.exec(c, "puke 1", { quietMs: 300 }), step));
  }
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: CheckAutomap read ${STEPS.map((s) => s.want).join(", ")}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsautomap", wad: buildAcsAutomapPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

// [rc4l] The renderer row says beside its value that the game must restart once it is changed, and only then: exit 0 when the note shares the row's line on GL and on the backend alone, and the old line of its own is gone.
//
// usage:  node scenarios/restartnote.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch, judgeNoteBeside } from "../src/template.mjs";
import { cvarValueFrom } from "../src/shot.mjs";
import { readMenu } from "../src/ui.mjs";
import * as cap from "../src/capture.mjs";

export function buildRestartNoteWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] The last side's setting says GL while -vulkan draws, so its change goes to the long "Vulkan (Recommended)", after which the note has to shrink to fit.
export const SIDES = [{ name: "gl", gl: true }, { name: "vk", noGl: true }, { name: "vk-set-gl", noGl: true, launchCvars: { vid_backend: 0 } }];

// [rc4l] Each read after a frame has drawn the menu as it now is; an unfocused window draws none without fua_render_in_background.
async function readAfter(c, command) {
  if (command) await cap.exec(c, command);
  await new Promise((r) => setTimeout(r, 300));
  return readMenu(c);
}

export async function judgeRestart(c, inst, label) {
  await cap.exec(c, "fua_render_in_background 1");
  const launched = cvarValueFrom(await cap.exec(c, "vid_backend"));
  const other = launched === "0" ? "1" : "0";
  const before = await readAfter(c, "openmenu VideoModeMenu");
  const changed = await readAfter(c, `vid_backend ${other}`);
  const back = await readAfter(c, `vid_backend ${launched}`);
  return judgeNoteBeside(`${label} vid_backend ${launched} to ${other}`, { before, changed, back }, { row: "Renderer", note: "(restart", absent: ["next launch"] });
}

runScenarioIfMain(import.meta.url, () => ({ name: "restartnote", wad: buildRestartNoteWad(), maps: ["MAP01"], sides: SIDES,
  judgeLaunch: judgeRestart }), compareEachLaunch);

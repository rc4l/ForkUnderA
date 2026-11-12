// [rc4l] Talking to a FRIENDLY actor (zandronum@4dcae1b00bf2): exit 0 when using it opens its conversation, with sv_shootthroughallies off and on.
//
// usage:  node scenarios/friendtalk.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import { readMenu } from "../src/ui.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FRIEND = "ZxFriend";
// [rc4l] The friend's line and its one choice, read back from the screen.
export const DIALOG = "ZX friendly hello", CHOICE = "ZX goodbye";
export const DECORATE = `actor ${FRIEND}\n{\n  Radius 20\n  Height 56\n  Health 100\n  +SOLID\n  +SHOOTABLE\n  +FRIENDLY\n  States\n  {\n  Spawn:\n    TROO A -1\n    Stop\n  }\n}\n`;
export const DIALOGUE = `namespace = "ZDoom";\nconversation\n{\n  actor = "${FRIEND}";\n  page\n  {\n    name = "Friend";\n    dialog = "${DIALOG}";\n    choice\n    {\n      text = "${CHOICE}";\n      closedialog = true;\n    }\n  }\n}\n`;

export function buildFriendTalkPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] The player faces the friend a third of TALKRANGE away.
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 160, angle: 270, type: 31100 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { extra: [{ name: "DIALOGUE", data: DIALOGUE }] })) },
    { name: "decorate.txt", data: DECORATE.replace(`actor ${FRIEND}`, `actor ${FRIEND} 31100`) },
  ]);
}

// [rc4l] What is wrong with the screen after using the friend: its line and choice are drawn.
export function judgeTalk(lines) {
  const texts = (lines || []).map((l) => l.text);
  const text = texts.join("\n");
  return text.includes(DIALOG) && text.includes(CHOICE) ? [] : [`using the friend opened no conversation (${texts.slice(0, 4).join(" | ").slice(0, 120)})`];
}

export const SIDES = [
  { name: "allies", launchCvars: { sv_shootthroughallies: 0 } },
  { name: "through", launchCvars: { sv_shootthroughallies: 1 } },
];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, "+use", { quietMs: 60 });
  await cap.waitTics(c, 3);
  await cap.exec(c, "-use", { quietMs: 60 });
  await cap.waitTics(c, 10);
  const faults = judgeTalk((await readMenu(c)).lines);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: using ${FRIEND} opened its conversation`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "friendtalk", wad: buildFriendTalkPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

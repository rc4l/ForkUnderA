// [rc4l] A player taking a name already in use (zandronum@1a6bab827d4b): exit 0 when the server renames them and tells them so in one sentence pair, the names in double quotes.
//
// usage:  node scenarios/netrename.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { readMenu } from "../src/ui.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const TAKEN = "zxalpha";
// [rc4l] How long to look for the message, since a client holds its own renames back for seconds.
export const LOOK_MS = 15000;

export function buildNetRenamePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] The renaming message among a HUD dump's messages, its lines joined and colour codes dropped, or null.
export function renameMessage(msgs) {
  const texts = (msgs || []).map((m) => String(m.text).replace(/\x1c(\[[^\]]*\]|.)/g, "").replace(/\s+/g, " ").trim());
  return texts.find((t) => t.includes("already in use")) ?? null;
}

// [rc4l] What is wrong with the message: it must name the taken name and the new one in double quotes, each sentence ended.
export function judgeMessage(text) {
  if (text === null) return ["the renamed player was never told"];
  return /^The name "zxalpha" is already in use\. You are renamed to "[^"]+"\.$/.test(text) ? [] : [`the message reads ${JSON.stringify(text)}`];
}

export async function judgeNetplay(set, label) {
  const [first, second] = set.clients;
  await cap.exec(first.c, `name ${TAKEN}`);
  await cap.waitTics(set.server.c, 140);
  await cap.exec(second.c, `name ${TAKEN}`);
  let text = null;
  for (const end = Date.now() + LOOK_MS; text === null && Date.now() < end; await cap.sleep(250)) text = renameMessage((await readMenu(second.c)).msgs);
  const faults = judgeMessage(text);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${text}`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "taken", players: 2 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netrename", wad: buildNetRenamePk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

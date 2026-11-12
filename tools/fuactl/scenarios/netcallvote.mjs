// [rc4l] A custom vote's submenu in the call vote menu (zandronum@a84419a6841a): exit 0 when closing every menu from the submenu and opening the call vote menu again still lists the vote once, where it was listed twice.
//
// usage:  node scenarios/netcallvote.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { readMenu } from "../src/ui.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const VOTE_LABEL = "ZX VOTE", VOTE_MENU = "ZXVoteMenu";
export const VOTEINFO = `votetype zxvote\n{\n\tAction = CallACS("zxvote");\n\tMenu = ${VOTE_MENU}, "${VOTE_LABEL}";\n}\n`;
export const MENUDEF = `OptionMenu ${VOTE_MENU}\n{\n\tTitle "ZX VOTE MENU"\n\tStaticText "zx inside"\n}\n`;

export function buildNetCallVotePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "voteinfo.txt", data: VOTEINFO },
    { name: "menudef.txt", data: MENUDEF },
  ]);
}

// [rc4l] How many of a menu read's lines are the vote's own entry, its submenu's title aside.
export function voteEntries(menu) {
  return (menu?.lines || []).filter((l) => String(l.text).trim().toUpperCase() === VOTE_LABEL).length;
}

// [rc4l] What is wrong with the two reads of the call vote menu, before and after closing every menu from the vote's submenu.
export function judgeVoteMenus(first, again) {
  const faults = [];
  if (voteEntries(first) !== 1) faults.push(`the call vote menu listed the vote ${voteEntries(first)} times when first opened`);
  if (voteEntries(again) !== 1) faults.push(`the call vote menu listed the vote ${voteEntries(again)} times when opened again`);
  return faults;
}

async function readAfter(c, command) {
  await cap.exec(c, command);
  await cap.sleep(300);
  return readMenu(c);
}

export async function judgeNetplay(set, label) {
  const client = set.clients[0].c;
  const first = await readAfter(client, "openmenu ZA_CallVote");
  await readAfter(client, `openmenu ${VOTE_MENU}`);
  await cap.exec(client, "closemenu");
  await cap.sleep(300);
  const again = await readAfter(client, "openmenu ZA_CallVote");
  await cap.exec(client, "closemenu");
  const faults = judgeVoteMenus(first, again);
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the vote is listed once, again after closing every menu from its submenu`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "submenu", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netcallvote", wad: buildNetCallVotePk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

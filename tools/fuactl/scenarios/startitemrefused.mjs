// [rc4l] A player class whose start item refuses it absolutely (zandronum@832625b0700b): exit 0 when the game starts with the player in the level without that item, where the refused pickup left who took it unset and the start stopped with "Cannot give morph items".
//
// usage:  node scenarios/startitemrefused.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const PLAYER_CLASS = "ZXPlayer", REFUSED = "ZXRefused";
export const DECORATE = `actor ${REFUSED} : Inventory
{
	Inventory.MaxAmount 1
	Inventory.ForbiddenTo "${PLAYER_CLASS}"
	+INVENTORY.RESTRICTABSOLUTELY
}
actor ${PLAYER_CLASS} : DoomPlayer
{
	Player.DisplayName "${PLAYER_CLASS}"
	Player.StartItem "Pistol"
	Player.StartItem "Fist"
	Player.StartItem "Clip", 50
	Player.StartItem "${REFUSED}"
}
`;
export const KEYCONF = `clearplayerclasses\naddplayerclass ${PLAYER_CLASS}\n`;

export function buildStartItemRefusedPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }, { name: "decorate.txt", data: DECORATE }, { name: "keyconf.txt", data: KEYCONF }]);
}

// [rc4l] What is wrong with the player's body class and printinv's list.
export function judgeStartItems({ bodyClass, inventory }) {
  const faults = [];
  if (bodyClass !== PLAYER_CLASS) faults.push(`the player is a ${bodyClass}, not a ${PLAYER_CLASS}`);
  if (!/\bPistol\b/.test(inventory)) faults.push("the player started without its pistol");
  if (new RegExp(`\\b${REFUSED}\\b`).test(inventory)) faults.push(`the player holds the ${REFUSED} that refuses it`);
  return faults;
}

export async function judgeLaunch(c, inst, label) {
  const body = ((await c.rpc("world.actors", { players: true }))?.actors || []).find((a) => a.player === 0);
  const inventory = await cap.exec(c, "printinv", { quietMs: 300 });
  const faults = judgeStartItems({ bodyClass: body?.c, inventory });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the game started without the item that refuses the player`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "startitemrefused", wad: buildStartItemRefusedPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

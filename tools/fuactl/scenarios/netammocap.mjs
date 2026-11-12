// [rc4l] SetAmmoCapacity on a server for ammo the player cannot take (zandronum@9d03d58bde70): exit 0 when the server lives through it and the script runs on past it.
//
// usage:  node scenarios/netammocap.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, addToCVar, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] The ammo, and the counter the script adds to once past the call.
export const STRINGS = ["ZXForbiddenAmmo", "zx_ammocap_ran"];
const AMMO = 0, RAN = 1;
// [rc4l] Forbidden to every Doom player, so GiveInventoryType hands back no item for the capacity to go on.
export const DECORATE = `actor ${STRINGS[AMMO]} : Ammo\n{\n  Inventory.MaxAmount 10\n  Inventory.ForbiddenTo "DoomPlayer"\n}\n`;

// [rc4l] ENTER: SetAmmoCapacity("ZXForbiddenAmmo", 50), then the counter.
export function buildScript() {
  return [PCD.PUSHNUMBER, AMMO, PCD.PUSHNUMBER, 50, PCD.SETAMMOCAPACITY, ...addToCVar(RAN, 1), PCD.TERMINATE];
}

export function buildNetAmmoCapPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.ENTER }], STRINGS) })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "cvarinfo.txt", data: `server int ${STRINGS[RAN]} = 0;\n` },
  ]);
}

export async function judgeNetplay(set, label) {
  await cap.waitTics(set.server.c, 35);
  const text = await cap.exec(set.server.c, STRINGS[RAN]);
  const ran = Number(/is "(\d+)"/.exec(String(text))?.[1]);
  return ran >= 1 ? { lines: [`PASS ${label}: the server ran past SetAmmoCapacity ${ran} time(s)`], failed: 0 }
    : { lines: [`FAIL ${label}: the script never got past SetAmmoCapacity (${String(text).trim()})`], failed: 1 };
}

export const CONFIGS = [{ name: "forbidden", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netammocap", wad: buildNetAmmoCapPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

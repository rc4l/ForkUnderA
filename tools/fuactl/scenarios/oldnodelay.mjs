// [Doruk-Aksoy] Probes whose 1-tic NoDelay spawn state starts a script that removes them, under compat_fua_oldnodelay: exit 0 when none reaches its second state, as in Zandronum 3.3, rather than starting a script the removal orphans.
//
// usage:  node scenarios/oldnodelay.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callSpecial, incrementCVar, PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter, MUTE } from "../src/template.mjs";

const PROBE = 20200;

// [Doruk-Aksoy] DnD's shared-item pattern: the NoDelay state's script removes its actor, and the next state's script counts the probes that got there.
export const DECORATE = `Actor ZXNoDelayProbe ${PROBE}
{
  +NOINTERACTION
  States
  {
  Spawn:
    TNT1 A 1 NoDelay ACS_ExecuteAlways(2, 0)
    TNT1 A 0 ACS_ExecuteAlways(3, 0)
    TNT1 A -1
    Stop
  }
}
`;

export function buildOldNoDelayPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const x of [128, 256, 384]) m.thing({ x, y: 384, angle: 90, type: PROBE });
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  const behavior = acsScripts([
    { code: [...callSpecial(SPECIAL.Thing_Remove, [0]), PCD.TERMINATE], open: false, number: 2 },
    { code: [...incrementCVar(0), PCD.TERMINATE], open: false, number: 3 },
  ], ["zx_nodelay_reached"]);
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "cvarinfo.txt", data: "server int zx_nodelay_reached = 0;\n" },
  ]);
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] Each of the three probes UZDoom's timing lets through counts one.
export const COUNTER = { command: "zx_nodelay_reached", label: "probes that reached their second state",
  pattern: "\"zx_nodelay_reached\" is \"(\\d+)\"", ceiling: 0 };

export const SIDES = [{ name: "old", launchCvars: { ...MUTE, compat_fua_oldnodelay: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "oldnodelay", wad: buildOldNoDelayPk3(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER, sides: SIDES }), compareCounter);

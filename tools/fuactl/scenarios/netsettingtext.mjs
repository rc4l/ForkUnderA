// [rc4l] The text a server prints for a float setting it changes (zandronum@ad1ae23c5589, zandronum@f7888a42ab35): exit 0 when infinity and NaN print by name and a finite value to the setting's decimals.
//
// usage:  node scenarios/netsettingtext.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

// [rc4l] Each change and the value its line must print: sv_coop_damagefactor takes two decimals and keeps any positive or NaN value, sv_gravity printf's six.
export const CHANGES = [
  { cvar: "sv_coop_damagefactor", set: "inf", want: "infinity" },
  { cvar: "sv_coop_damagefactor", set: "nan", want: "NaN" },
  { cvar: "sv_coop_damagefactor", set: "2.5", want: "2.50" },
  { cvar: "sv_coop_damagefactor", set: "1", want: "1.00" },
  { cvar: "sv_gravity", set: "700", want: "700.000000" },
  { cvar: "sv_gravity", set: "800", want: "800.000000" },
];

export function buildNetSettingTextPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384]]) m.thing({ x, y: 64, angle: 90, type });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] What is wrong with the server's reply to one change: its "changed to" line names the wanted value.
export function judgeChange(reply, { cvar, set, want }) {
  const m = new RegExp(`${cvar} changed to: (.*)`).exec(String(reply));
  if (!m) return [`setting ${cvar} to ${set} printed no change (${String(reply).trim().slice(0, 80)})`];
  return m[1].trim() === want ? [] : [`setting ${cvar} to ${set} printed ${JSON.stringify(m[1].trim().slice(0, 40))}, not ${want}`];
}

export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  const faults = [];
  for (const change of CHANGES) faults.push(...judgeChange(await cap.exec(sc, `${change.cvar} ${change.set}`), change));
  return { lines: [faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the server printed ${CHANGES.map((c) => c.want).join(", ")}`], failed: faults.length ? 1 : 0 };
}

export const CONFIGS = [{ name: "lan", players: 1 }];

runScenarioIfMain(import.meta.url, () => ({ name: "netsettingtext", wad: buildNetSettingTextPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

// [rc4l] Skulltag's assist under sv_requireskulltoscore 0 (zandronum@00538ee632fb): exit 0 when returning your skull while carrying the enemy's earns no assist with the cvar off, and still does with it on.
//
// usage:  node scenarios/skullassist.mjs
import { UdmfMap, wad, mapLumps, pk3, teamInfo } from "../src/udmf.mjs";
import { acsScripts, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] GetTeamProperty's TPROP_Carrier and TPROP_Assister, and Blue's and Red's places in the template's TEAMINFO.
export const TPROP = { Carrier: 8, Assister: 9 };
export const TEAM = { Blue: 0, Red: 1 };
// [rc4l] The template's own team start numbers, which its TEAMINFO gives Blue and Red.
export const TEAM_STARTS = { Blue: 31001, Red: 31002 };
export const TEAMINFO = teamInfo({ Blue: [`PlayerStartThingNumber ${TEAM_STARTS.Blue}`], Red: [`PlayerStartThingNumber ${TEAM_STARTS.Red}`] });
export const BOT = "ZXBot";
export const BOTINFO = `{\n  name = "${BOT}"\n  revealed = true\n}\n`;
export const STRINGS = ["carrier ", " assister "];

// [rc4l] Script 1 logs who carries Red's skull and who Blue's assist would go to, -1 for nobody.
export function buildScript() {
  const prop = (s, t, p) => [PCD.PUSHNUMBER, s, PCD.PRINTSTRING, PCD.PUSHNUMBER, t, PCD.PUSHNUMBER, p, PCD.CALLFUNC, 2, ACSF.GetTeamProperty, PCD.PRINTNUMBER];
  return [PCD.BEGINPRINT, ...prop(0, TEAM.Red, TPROP.Carrier), ...prop(1, TEAM.Blue, TPROP.Assister), PCD.ENDLOG, PCD.TERMINATE];
}

export function buildSkullAssistPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] A player start, Blue's and Red's team starts, at opposite walls.
  for (const [type, y] of [[1, 256], [TEAM_STARTS.Blue, 64], [TEAM_STARTS.Red, 448]]) m.thing({ x: 256, y, angle: 90, type });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], STRINGS) })) },
    { name: "botinfo.txt", data: BOTINFO },
    { name: "teaminfo.txt", data: TEAMINFO },
  ]);
}

// [rc4l] What is wrong with script 1's log: the console player carries Red's skull, and Blue's assist goes to `want`.
export function judgeLog(log, want) {
  const m = /carrier (-?\d+) assister (-?\d+)/.exec(String(log));
  if (!m) return [`script 1 logged no carrier and assister (${String(log).trim().slice(0, 120)})`];
  const faults = Number(m[1]) === 0 ? [] : [`Red's skull is carried by ${m[1]}, not the console player`];
  if (Number(m[2]) !== want) faults.push(`Blue's assist goes to ${m[2]}, not ${want}`);
  return faults;
}

const SKULLTAG = { skulltag: 1, sv_cheats: 1 };
// [rc4l] The cvar on keeps the assist for player 0, off it goes to nobody.
export const SIDES = [
  { name: "require", launchCvars: { ...SKULLTAG, sv_requireskulltoscore: 1 }, want: 0 },
  { name: "free", launchCvars: { ...SKULLTAG, sv_requireskulltoscore: 0 }, want: -1 },
];

export async function judgeLaunch(c, inst, label) {
  const side = SIDES.find((s) => label.endsWith(` ${s.name}`));
  await cap.waitTics(c, 10);
  await cap.exec(c, "changeteam Blue");
  // [rc4l] A bot joins the team with fewer players, Red, so Red's skull can be taken.
  await cap.exec(c, `addbot ${BOT}`);
  await cap.waitTics(c, 35);
  await cap.exec(c, "give RedSkullST");
  // [rc4l] A given skull is dropped, so giving Blue its own returns it.
  await cap.exec(c, "give BlueSkullST");
  await cap.waitTics(c, 5);
  const faults = judgeLog(await cap.exec(c, "puke 1", { quietMs: 300 }), side.want);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: carrying Red's skull and returning Blue's leaves Blue's assist with ${side.want}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "skullassist", wad: buildSkullAssistPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

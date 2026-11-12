// [rc4l] A map's winnermusic and a team's WinnerTheme, each with a track (zandronum@59b494f63d58): exit 0 when TEAMINFO's theme tracks read back through GetTeamProperty and a won deathmatch's intermission plays the map's winnermusic at its track, a won team game's the team's theme at its own, where Skulltag's d_stwin played at track 0, CTF's too (zandronum@9a8531095e0b).
//
// usage:  node scenarios/intermissiontheme.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3, teamInfo } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acsScripts, ACSF, PCD, SCRIPT, callSpecial } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] The map's winner music and the teams' themes, each with the track it names, and the lumps they play.
export const MAP_WIN = { name: "ZXWIN", order: 3 }, TEAM_WIN = { name: "ZXTEAMW", order: 4 }, TEAM_LOSE = { name: "ZXTEAML", order: 2 };
// [rc4l] GetTeamProperty's TPROP_WinnerThemeOrder and TPROP_LoserThemeOrder, after TPROP_LoserTheme's 17.
export const TPROP = { WinnerThemeOrder: 18, LoserThemeOrder: 19 };
export const STRINGS = ["orders", " "];

export const MAPINFO = `map MAP01 "Theme"\n{\n  next = "MAP02"\n  winnermusic = "${MAP_WIN.name}", ${MAP_WIN.order}\n}\n\nmap MAP02 "After"\n{\n}\n`;

// [rc4l] Blue and Red as the core TEAMINFO has them less their team starts, which a deathmatch start stands in for, Blue's themes written each way TEAMINFO takes a track.
// [rc4l] The template's own team start numbers, which a CTF game spawns its teams at.
export const TEAM_STARTS = { Blue: 31001, Red: 31002 };
export const TEAMINFO = teamInfo({
  Blue: [`WinnerTheme "${TEAM_WIN.name}:${TEAM_WIN.order}"`, `LoserTheme "${TEAM_LOSE.name}" ${TEAM_LOSE.order}`, `PlayerStartThingNumber ${TEAM_STARTS.Blue}`],
  Red: [`PlayerStartThingNumber ${TEAM_STARTS.Red}`],
});

// [rc4l] Script 1 logs "orders" and Blue's and Red's winner and loser theme tracks; script 2 ends the level.
export function buildScript() {
  const prop = (t, p) => [PCD.PUSHNUMBER, 1, PCD.PRINTSTRING, PCD.PUSHNUMBER, t, PCD.PUSHNUMBER, p, PCD.CALLFUNC, 2, ACSF.GetTeamProperty, PCD.PRINTNUMBER];
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING,
    ...[0, 1].flatMap((t) => [...prop(t, TPROP.WinnerThemeOrder), ...prop(t, TPROP.LoserThemeOrder)]), PCD.ENDLOG, PCD.TERMINATE];
}

export function buildIntermissionThemePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 256, angle: 90, type: 11 });
  for (const [type, x] of [[TEAM_STARTS.Blue, 128], [TEAM_STARTS.Red, 384]]) m.thing({ x, y: 448, angle: 270, type });
  const scripts = [{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }, { code: [...callSpecial(SPECIAL.Exit_Normal, [0]), PCD.TERMINATE], type: SCRIPT.CLOSED, number: 2 }];
  const after = new UdmfMap();
  after.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  after.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", after)) },
    { name: "mapinfo.txt", data: MAPINFO },
    { name: "teaminfo.txt", data: TEAMINFO },
    // [rc4l] Music recorded at volume 0 is never decoded, so any bytes stand for a song.
    ...[MAP_WIN, TEAM_WIN, TEAM_LOSE].map((s) => ({ name: `music/${s.name.toLowerCase()}.mid`, data: Buffer.from("MThd") })),
  ]);
}

// [rc4l] What is wrong with script 1's log: Blue's tracks as TEAMINFO gave them, Red's none.
export function judgeOrders(log) {
  const m = /orders (-?\d+) (-?\d+) (-?\d+) (-?\d+)/.exec(String(log));
  if (!m) return [`script 1 logged no theme tracks (${String(log).trim().slice(0, 120)})`];
  const want = [TEAM_WIN.order, TEAM_LOSE.order, 0, 0];
  return m.slice(1).map(Number).join(" ") === want.join(" ") ? [] : [`the theme tracks read ${m.slice(1).join(" ")}, not ${want.join(" ")}`];
}

// [rc4l] What is wrong with fua_music's report against the music wanted.
export function judgeMusic(reply, want) {
  const m = /music "([^"]*)" order (-?\d+)/.exec(String(reply));
  if (!m) return [`fua_music reported nothing (${String(reply).trim().slice(0, 120)})`];
  return m[1].toUpperCase() === want.name && Number(m[2]) === want.order ? [] : [`the intermission plays "${m[1]}" at track ${m[2]}, not "${want.name}" at ${want.order}`];
}

// [rc4l] A won deathmatch takes the map's winner music, a won team game its team's theme once the player has joined Blue, CTF's tied score a win.
export const SIDES = [
  { name: "dm", launchCvars: { deathmatch: 1 }, want: MAP_WIN },
  { name: "team", launchCvars: { deathmatch: 1, teamplay: 1 }, commands: ["changeteam Blue"], want: TEAM_WIN },
  { name: "ctf", launchCvars: { ctf: 1 }, commands: ["changeteam Blue"], want: TEAM_WIN },
];

export async function judgeLaunch(c, inst, label) {
  const side = SIDES.find((s) => label.endsWith(` ${s.name}`));
  await cap.waitTics(c, 10);
  for (const command of side.commands || []) await cap.exec(c, command);
  await cap.waitTics(c, 5);
  const faults = judgeOrders(await cap.exec(c, "puke 1", { quietMs: 300 }));
  await cap.exec(c, "puke 2");
  // [rc4l] Long enough for the intermission to choose its music, short of anything moving it on.
  await cap.waitTics(c, 20);
  faults.push(...judgeMusic(await cap.exec(c, "fua_music", { quietMs: 300 }), side.want));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the theme tracks read back and the intermission plays ${side.want.name} at track ${side.want.order}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "intermissiontheme", wad: buildIntermissionThemePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

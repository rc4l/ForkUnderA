// [rc4l] Three player classes in a team game, one for any team, one for blue alone and one for red alone (zandronum@469e5f569641): exit 0 when a player whose class belongs to the other team has it replaced on opening Player Setup, and its Class row cycles only through Random, the open class and their own team's.
//
// usage:  node scenarios/playerclassmenu.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { menuNav } from "../src/ui.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };

// [rc4l] Each class's display name and the team index it is limited to, or null for any team; team 0 is Blue and team 1 Red.
export const CLASSES = [
  { actor: "ZXAnyPlayer", name: "AnyTeam", team: null },
  { actor: "ZXBluePlayer", name: "BlueOnly", team: 0 },
  { actor: "ZXRedPlayer", name: "RedOnly", team: 1 },
];

export const DECORATE = CLASSES.map((k) => `Actor ${k.actor} : DoomPlayer
{
  Player.DisplayName "${k.name}"${k.team === null ? "" : `\n  LimitedToTeam ${k.team}`}
}
`).join("\n");

export const MAPINFO = `GameInfo
{
  PlayerClasses = ${CLASSES.map((k) => `"${k.actor}"`).join(", ")}
}
map MAP01 "Class room"
{
}
`;

// [rc4l] A 1024x512 room with player starts and the deathmatch starts a team game without team starts uses.
export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: WALL }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384], [11, 512], [11, 640], [11, 768]]) m.thing({ x, y: 64, angle: 90, type });
  return m;
}

export function buildPlayerClassMenuPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap())) },
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: MAPINFO },
  ]);
}

// [rc4l] The team index a `team` print names, or -1.
export function teamOf(text) {
  if (/\bblue\b/i.test(text)) return 0;
  if (/\bred\b/i.test(text)) return 1;
  return -1;
}

export const classCvar = (text) => (/"playerclass" is "([^"]*)"/.exec(text) || [])[1] ?? null;

// [rc4l] What is wrong with the class a player was moved to and the names its Class row cycled through.
export function judgeClasses(team, opened, cycled) {
  const faults = [];
  if (team < 0) return ["is on no team"];
  const mine = CLASSES.find((k) => k.team === team).name;
  const theirs = CLASSES.find((k) => k.team === 1 - team).name;
  if (opened !== "AnyTeam") faults.push(`opening Player Setup left its class at ${opened}, not AnyTeam`);
  if (cycled.includes(theirs)) faults.push(`its Class row offers ${theirs}`);
  for (const want of ["Random", "AnyTeam", mine]) if (!cycled.includes(want)) faults.push(`its Class row never offers ${want}`);
  return faults;
}

// [rc4l] Player Setup's selectable rows run Name, Color, Red, Green, Blue, then Class.
const DOWN_TO_CLASS = 5;

export async function judgeNetplay(set) {
  const lines = [];
  let failed = 0;
  for (const client of set.clients) {
    const c = client.c;
    const team = teamOf(await cap.exec(c, "team"));
    const theirs = CLASSES.find((k) => k.team === 1 - team);
    if (theirs) await cap.exec(c, `playerclass ${theirs.name}`);
    await cap.exec(c, "openmenu PlayerMenu", { quietMs: 400 });
    const opened = classCvar(await cap.exec(c, "playerclass"));
    await menuNav(c, Array(DOWN_TO_CLASS).fill("down"), { delay: 120 });
    const cycled = [];
    for (let i = 0; i < 4; i++) {
      await menuNav(c, ["right"], { delay: 150 });
      cycled.push(classCvar(await cap.exec(c, "playerclass")));
    }
    await cap.exec(c, "closemenu");
    const faults = judgeClasses(team, opened, cycled);
    const name = `${client.role} player ${client.player} on team ${team}`;
    lines.push(faults.length ? `FAIL ${name}: ${faults.join("; ")}` : `PASS ${name}: moved to AnyTeam, cycles ${cycled.join(", ")}`);
    if (faults.length) failed++;
  }
  return { lines, failed };
}

export const CONFIGS = [{ name: "teamgame", players: 1, observers: 0, serverCvars: { teamplay: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "playerclassmenu", wad: buildPlayerClassMenuPk3(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

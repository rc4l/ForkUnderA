// [rc4l] A player class drawn additively, after respawn invulnerability's flicker (zandronum@1a825432a72b): exit 0 when the player is additive again once it ends or they die, where they came back plain.
//
// usage:  node scenarios/respawnstyle.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { AcsScript, acsScripts, callSpecial, PCD, SCRIPT, ACSF } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { cvarValueFrom } from "../src/shot.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const CLASS = "ZxAddPlayer";
export const DECORATE = `actor ${CLASS} : DoomPlayer\n{\n  RenderStyle Add\n}\n`;
export const MAPINFO = `gameinfo\n{\n  playerclasses = "${CLASS}"\n}\n\nmap MAP01 "Style"\n{\n}\n`;
// [rc4l] GetActorProperty's numbers for APROP_RenderStyle and the legacy styles it answers with.
export const APROP_RENDERSTYLE = 4, STYLE = { Normal: 1, Translucent: 64, Add: 65 };
export const STRINGS = ["zx_style_during", "zx_style_after", "zx_style_dead", "zx_style_kill"];
const DURING = 0, AFTER = 1, DEAD = 2, KILL = 3;
// [rc4l] Tics into the level each style is read: inside the three seconds of invulnerability, then past them; a killed player's corpse is read DEAD_TICS after dying.
export const DURING_TICS = 35, AFTER_TICS = 175, DEAD_TICS = 10;

// [rc4l] The words of SetCVar(strings[i], GetActorProperty(0, APROP_RenderStyle)).
const recordStyle = (i) => [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, APROP_RENDERSTYLE, PCD.GETACTORPROPERTY, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP];

// [rc4l] ENTER: the player's style a second in, the corpse's when zx_style_kill has the script kill them then, and the style after the invulnerability.
export function enterScript() {
  const s = new AcsScript();
  s.emit(PCD.DELAYDIRECT, DURING_TICS, ...recordStyle(DURING), PCD.PUSHNUMBER, KILL, PCD.GETCVAR);
  s.jump(PCD.IFNOTGOTO, "alive").emit(...callSpecial(SPECIAL.DamageThing, [0, 0]), PCD.DELAYDIRECT, DEAD_TICS, ...recordStyle(DEAD));
  s.label("alive").emit(PCD.DELAYDIRECT, AFTER_TICS - DURING_TICS, ...recordStyle(AFTER), PCD.TERMINATE);
  return s.words(0);
}

export function buildRespawnStylePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 256, angle: 90, type: 11 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: enterScript(), type: SCRIPT.ENTER, number: 1 }], STRINGS) })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "mapinfo.txt", data: MAPINFO },
    { name: "cvarinfo.txt", data: STRINGS.map((s, i) => `server int ${s} = ${i === KILL ? 0 : -1};\n`).join("") },
  ]);
}

// [rc4l] What is wrong with the styles read: translucent while flickering, then additive once it ends, or on the corpse when the player was killed.
export function judgeStyles({ during, after, dead }, killed) {
  const faults = [];
  if (during !== STYLE.Translucent) faults.push(`the player's style during invulnerability is ${during}, not ${STYLE.Translucent} (translucent)`);
  if (killed && dead !== STYLE.Add) faults.push(`the corpse's style is ${dead}, not ${STYLE.Add} (additive)`);
  if (!killed && after !== STYLE.Add) faults.push(`the player's style after it is ${after}, not ${STYLE.Add} (additive)`);
  return faults;
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, AFTER_TICS + 20);
  const read = async (name) => Number(cvarValueFrom(await cap.exec(c, name)));
  const killed = label.endsWith("killed");
  const faults = judgeStyles({ during: await read(STRINGS[DURING]), after: await read(STRINGS[AFTER]), dead: await read(STRINGS[DEAD]) }, killed);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: translucent while flickering, additive ${killed ? "as a corpse" : "after"}`, failed: faults.length ? 1 : 0 };
}

// [rc4l] A deathmatch, where a player enters with respawn invulnerability, and one where the script kills them during it.
export const SIDES = [{ name: "dm", launchCvars: { deathmatch: 1, cl_respawninvuleffect: 1 } }, { name: "killed", launchCvars: { deathmatch: 1, cl_respawninvuleffect: 1, zx_style_kill: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "respawnstyle", wad: buildRespawnStylePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);

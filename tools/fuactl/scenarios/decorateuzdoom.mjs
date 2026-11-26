// [Doruk-Aksoy] UZDoom's DECORATE features ours lacked (features/decorate-uzdoom), one check actor each that spawns a pass or fail marker: exit 0 when every check passes offline, on a server and on each of its clients.
//
// usage:  node scenarios/decorateuzdoom.mjs [--uzdoom]   (--uzdoom runs the checks ours shares with UZDoom in a UZDoom build instead)
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, compareNetplay, isMain } from "../src/template.mjs";
import { runUzdoom } from "../src/uzdoom.mjs";
import * as cap from "../src/capture.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const ROOM_SIZE = 2048, SPACING = 192, FIRST_NUM = 31000;
// [Doruk-Aksoy] ACS's SetUserVariable and GetUserVariable function numbers (p_acs.cpp EACSFunctions).
const ACSF_SET_USER_VAR = 24, ACSF_GET_USER_VAR = 25;
export const STRINGS = ["user_f", "user_i", "user_h"];

// [Doruk-Aksoy] An actor written a line each: its properties, then its Spawn states, a label being a line ending in a colon.
export function actorDef(name, props = [], states = ["TNT1 A -1", "Stop"], num = null) {
  const lines = [`actor ${name}${num === null ? "" : ` ${num}`}`, "{", ...props.map((p) => `\t${p}`), "\tStates", "\t{", "\tSpawn:",
    ...states.map((st) => (/:$/.test(st) ? `\t${st}` : `\t\t${st}`)), "\t}", "}"];
  return lines.join("\n") + "\n";
}

const dummy = (name, props = []) => actorDef(name, ["+NOGRAVITY", "+NOINTERACTION", ...props]);
const spawn = (cls, x, y, z, flags = "SXF_NOCHECKPOSITION") => `TNT1 A 0 A_SpawnItemEx("${cls}", ${x}, ${y}, ${z}, 0, 0, 0, 0, ${flags})`;
// [Doruk-Aksoy] A dummy at an offset that becomes self's target, master and tracer.
const pointAt = (cls, x, y, z) => spawn(cls, x, y, z, "SXF_NOCHECKPOSITION|SXF_ISTARGET|SXF_ISMASTER|SXF_ISTRACER");
// [Doruk-Aksoy] A_JumpIf on an expression, true jumping to Pass.
const jumpIf = (expr) => `TNT1 A 0 A_JumpIf(${expr}, "Passed")`;
const near = (expr, want, eps = 0.001) => `abs((${expr}) - (${want})) < ${eps}`;
const all = (...parts) => parts.join(" && ");
// [Doruk-Aksoy] The states with which an actor other than the check gives check `id` its verdict.
const verdictFrom = (kind, id) => [`TNT1 A 0 A_Log("zxcheck ${id} ${kind}")`, spawn(`Zx${kind === "pass" ? "Pass" : "Fail"}_${id}`, 0, 0, 0), "TNT1 A -1", "Stop"];
// [Doruk-Aksoy] A_CheckFlag on each flag in turn, any one unset going to Failed.
// [Doruk-Aksoy] The sprite flip render flags (renderflags at UZDoom's bits).
const FLIP_FLAGS = ["XFLIP", "YFLIP", "SPRITEFLIP", "DONTFLIP"];
const allFlagsSet = (flags) => flags.flatMap((f, i) => [`TNT1 A 0 A_CheckFlag("${f}", "Flag${i}")`, "Goto Failed", `Flag${i}:`]).concat(["Goto Passed"]);
// [Doruk-Aksoy] UZDoom's gameplay flags now at its bits (features/decorate-uzdoom).
export const NEW_FLAGS = ["GETOWNER", "LAXTELEFRAGDMG", "ICESHATTER", "SMASHABLE", "NOSHIELDREFLECT", "FORCEZERORADIUSDMG", "NOINFIGHTSPECIES", "FORCEINFIGHTING",
  "FRIGHTENING", "DONTFACETALKER", "HITOWNER", "RETARGETAFTERSLAM", "STOPRAILS", "MINVISIBLE", "MVISBLOCKED", "DONTFOLLOWPLAYERS", "SEEFRIENDLYMONSTERS", "ONLYSLAMSOLID",
  "NOAUTOOFFSKULLFLY", "DONTBOUNCEONSHOOTABLES", "BOUNCEONUNRIPPABLES", "DONTBOUNCEONSKY", "KEEPBOUNCEANGLE", "BOUNCEMODIFIESPITCH"];
const solidBox = (name, extra = []) => actorDef(name, ["+SOLID", "+NOGRAVITY", "Radius 16", "Height 56", ...extra]);

// [Doruk-Aksoy] Each check: its states (ending at Pass, or falling to Fail), its properties, extra actors, and fua when only ours has it.
export const CHECKS = [
  { id: "minmax", what: "Min, Max (int when every argument is)", states: [jumpIf(all("Min(3, -2, 7) == -2", "Max(3, -2, 7, 4) == 7", "Min(7, 3) / 2 == 1"))] },
  { id: "clamp", what: "Clamp", states: [jumpIf(all("Clamp(15, 0, 10) == 10", "Clamp(-5, 0, 10) == 0", "Clamp(5, 10, 0) == 0"))] },
  { id: "rounding", what: "Floor, Ceil, Round", states: [jumpIf(all("Floor(1.5) == 1", "Floor(-1.5) == -2", "Ceil(1.25) == 2", "Round(2.5) == 3", "Round(-2.5) == -3"))] },
  { id: "trig", what: "Tan, ACos, ASin, ATan in degrees", states: [jumpIf(all(near("Tan(45)", 1), near("ACos(0)", 90), near("ASin(1)", 90), near("ATan(1)", 45)))] },
  { id: "atan2", what: "ATan2, VectorAngle", states: [jumpIf(all(near("ATan2(1, 0)", 90), near("VectorAngle(0, 1)", 90), near("VectorAngle(-1, -1)", -135)))] },
  { id: "explog", what: "Exp, Log, Log10, CosH, SinH, TanH",
    states: [jumpIf(all(near("Log(Exp(2))", 2), near("Log10(1000)", 3), near("CosH(0)", 1), near("SinH(0)", 0), near("TanH(100)", 1)))] },
  { id: "countinv", what: "CountInv", defs: actorDef("ZxToken : Inventory", ["Inventory.MaxAmount 10"]) + actorDef("ZxToken2 : Inventory"),
    states: ['TNT1 A 0 A_GiveInventory("ZxToken", 3)', jumpIf(all('CountInv("ZxToken") == 3', 'CountInv("ZxToken2") == 0'))] },
  { id: "health", what: "GetSpawnHealth, GetGibHealth", props: ["Health 77", "GibHealth 20"], states: [jumpIf(all("GetSpawnHealth() == 77", "GetGibHealth() == -20"))] },
  { id: "missiledamage", what: "GetMissileDamage", props: ["Damage 5"], states: [jumpIf("GetMissileDamage(0, 1) == 5")] },
  // [Doruk-Aksoy] A_SpawnItemEx's positive yofs is to the right, as UZDoom's xofs*s - yofs*c puts it, so the dummy is south of a check facing east.
  { id: "distangle", what: "GetDistance, GetAngle", defs: dummy("ZxDistDummy"),
    states: [pointAt("ZxDistDummy", 0, 64, 32), jumpIf(all(near("GetDistance(false)", 64), near("GetDistance(true)", Math.hypot(64, 32)),
      near("GetAngle(0)", -90), near("GetAngle(GAF_RELATIVE)", -90), near("GetAngle(GAF_SWITCH)", 90)))] },
  { id: "zat", what: "GetZAt on the floor, a step and the ceiling", step: true,
    states: [jumpIf(all("GetZAt(0) == 0", "GetZAt(96, 0) == 32", "GetZAt(0, 0, 0, GZF_CEILING) == 256", "GetZAt(96, 0, 0, GZF_ABSOLUTEANG) == 32"))] },
  { id: "playerish", what: "GetCrouchFactor, GetPlayerInput, OverlayX/Y on a monster",
    states: [jumpIf(all("GetCrouchFactor(AAPTR_DEFAULT) == 1", "GetPlayerInput(INPUT_BUTTONS, AAPTR_DEFAULT) == 0", "OverlayX(0) == 0", "OverlayY(0) == 0"))] },
  { id: "cvar", what: "GetCVar", states: [jumpIf('GetCVar("sv_gravity") == 800')] },
  { id: "countprox", what: "CountProximity", defs: dummy("ZxProxA"),
    states: [spawn("ZxProxA", 40, 0, 0), spawn("ZxProxA", 0, 40, 0), spawn("ZxProxA", -40, 0, 0), spawn("ZxProxA", 0, -90, 0),
      jumpIf(all('CountProximity("ZxProxA", 60) == 3', 'CountProximity("ZxProxA", 100) == 4'))] },
  { id: "checkprox", what: "A_CheckProximity", defs: dummy("ZxProxB"),
    states: [spawn("ZxProxB", 40, 0, 0), spawn("ZxProxB", -40, 0, 0), 'TNT1 A 0 A_CheckProximity("Failed", "ZxProxB", 60, 3)',
      'TNT1 A 0 A_CheckProximity("Passed", "ZxProxB", 60, 2, CPXF_EXACT)'] },
  { id: "fields", what: "the actor fields UZDoom's DECORATE reads", props: ["Gravity 0.5", "MaxStepHeight 30", "MaxDropOffHeight 40", "PainThreshold 7",
    "BounceFactor 0.5", "DamageFactor 0.25", "ProjectilePassHeight 10", "MeleeThreshold 64", "MaxTargetRange 512", "FloatSpeed 7", "Threshold 42"],
    states: [jumpIf(all("gravity == 0.5", "MaxStepHeight == 30", "MaxDropOffHeight == 40", "PainThreshold == 7", "bouncefactor == 0.5", "DamageFactor == 0.25",
      "projectilepassheight == 10", "meleethreshold == 64", "maxtargetrange == 512", "FloatSpeed == 7", "Friction == 1", "threshold == 42"))] },
  { id: "tics", what: "tics", states: ['TNT1 A 5 A_JumpIf(tics == 5, "Passed")'] },
  { id: "uservarfloat", what: "float user variables and their setters", vars: ["var float user_f;", "var float user_a[3];", "var int user_i;", "var float user_g;"],
    states: ['TNT1 A 0 A_SetUserVarFloat("user_f", 1.25)', 'TNT1 A 0 A_SetUserArrayFloat("user_a", 2, -0.5)', 'TNT1 A 0 A_SetUserVarFloat("user_i", 2.9)',
      'TNT1 A 0 A_SetUserVar("user_g", 3)', jumpIf(all("user_f == 1.25", "user_a[2] == -0.5", "user_a[0] == 0", "user_i == 2", "user_g == 3"))] },
  { id: "uservaracs", what: "ACS reading and writing a float user variable as 16.16", vars: ["var float user_f;", "var int user_i;", "var float user_h;"],
    states: ['TNT1 A 0 A_SetUserVarFloat("user_f", 1.25)', 'TNT1 A 0 A_SetUserVar("user_i", 7)', 'TNT1 A 0 ACS_NamedExecuteWithResult("zxsetfloat")',
      jumpIf(all('ACS_NamedExecuteWithResult("zxgetfloat") == 81920', 'ACS_NamedExecuteWithResult("zxgetint") == 7', "user_h == 2.5"))] },
  { id: "selfdamage", what: "A_DamageSelf's src and SelfDamageFactor", props: ["Health 100", "+SHOOTABLE", "PainChance 0", "SelfDamageFactor 0.5"],
    states: ['TNT1 A 0 A_DamageSelf(10, "None", 0, "None", "None", AAPTR_DEFAULT, AAPTR_DEFAULT)', 'TNT1 A 0 A_JumpIf(health != 95, "Failed")',
      'TNT1 A 0 A_DamageSelf(10, "None", 0, "None", "None", AAPTR_NULL, AAPTR_NULL)', jumpIf("health == 85")] },
  { id: "explode", what: "A_Explode's damagetype and nailrange, XF_EXPLICITDAMAGETYPE still hurting", verdictBy: "ZxBlastTarget",
    defs: actorDef("ZxBlastTarget", ["Health 100", "+SHOOTABLE", "+NOGRAVITY", "PainChance 0", "Radius 8", "Height 16"],
      ["TNT1 A 4", 'TNT1 A 0 A_JumpIf(health < 100, "Hit")', "TNT1 A -1", "Stop", "Hit:", 'TNT1 A 0 A_Log("zxcheck explode pass")',
        spawn("ZxPass_explode", 0, 0, 0), "TNT1 A -1", "Stop"]),
    states: [spawn("ZxBlastTarget", 48, 0, 0), "TNT1 A 1",
      'TNT1 A 0 A_Explode(40, 128, XF_EXPLICITDAMAGETYPE|XF_NOSPLASH, false, 0, 0, 10, "BulletPuff", "None", MISSILERANGE)', "TNT1 A -1", "Stop"] },
  { id: "closernoz", what: "A_JumpIfCloser's noz", defs: dummy("ZxHighDummy", ["Height 16"]),
    states: [pointAt("ZxHighDummy", 50, 0, 200), 'TNT1 A 0 A_JumpIfCloser(100, "Failed")', 'TNT1 A 0 A_JumpIfCloser(100, "Passed", true)'] },
  { id: "setscale", what: "A_SetScale's usezero", scale: [2, 0], states: ["TNT1 A 0 A_SetScale(2, 0, AAPTR_DEFAULT, true)", jumpIf(all("scaleX == 2", "scaleY == 0"))] },
  { id: "spawnis", what: "A_SpawnItemEx's SXF_ISTARGET, ISMASTER, ISTRACER", defs: dummy("ZxIsDummy"),
    states: [pointAt("ZxIsDummy", 64, 0, 0), jumpIf(all("IsPointerEqual(AAPTR_TARGET, AAPTR_MASTER)", "IsPointerEqual(AAPTR_MASTER, AAPTR_TRACER)", near("GetDistance(false)", 64)))] },
  { id: "species", what: "A_CheckSpecies, A_SetSpecies", props: ['Species "ZxSpec"'],
    states: ['TNT1 A 0 A_CheckSpecies("Next", "ZxSpec")', "Goto Failed", "Next:", 'TNT1 A 0 A_SetSpecies("ZxOther")', 'TNT1 A 0 A_CheckSpecies("Failed", "ZxSpec")',
      'TNT1 A 0 A_CheckSpecies("Passed", "ZxOther")'] },
  { id: "highlow", what: "A_JumpIfHigherOrLower", defs: dummy("ZxUpDummy", ["Height 16"]), props: ["Height 56"],
    states: [pointAt("ZxUpDummy", 32, 0, 100), 'TNT1 A 0 A_JumpIfHigherOrLower("Passed", "Failed")'] },
  // [Doruk-Aksoy] Solid, since an actor that is not passes through solid ones in UZDoom too.
  { id: "checkblock", what: "A_CheckBlock", props: ["+SOLID", "Radius 16", "Height 56"], defs: actorDef("ZxBlocker", ["+SOLID", "+NOGRAVITY", "Radius 16", "Height 56"]),
    states: [spawn("ZxBlocker", 40, 0, 0), 'TNT1 A 0 A_CheckBlock("Failed", 0, AAPTR_DEFAULT, -80, 0)', 'TNT1 A 0 A_CheckBlock("Passed", 0, AAPTR_DEFAULT, 30, 0)'] },
  { id: "setters", what: "A_SetFloatSpeed, A_SetPainThreshold", states: ["TNT1 A 0 A_SetFloatSpeed(9)", "TNT1 A 0 A_SetPainThreshold(4)", jumpIf(all("FloatSpeed == 9", "PainThreshold == 4"))] },
  { id: "logs", what: "A_Log, A_LogInt and A_LogFloat's local, and A_LogFloat", states: ['TNT1 A 0 A_Log("zxlog", false)', "TNT1 A 0 A_LogInt(3, true)", "TNT1 A 0 A_LogFloat(1.5)", "Goto Passed"] },
  { id: "props", what: "the class properties load (groups, MissileChanceMult, StealthAlpha, FriendlySeeBlocks, MinRespawnTics, RespawnDice, DefThreshold)",
    props: ["InfightingGroup 1", "ProjectileGroup -1", "SplashGroup 2", "MissileChanceMult 0.5", "StealthAlpha 0.25", "FriendlySeeBlocks 4", "MinRespawnTics -2",
      "RespawnDice 8", "DefThreshold 50"], states: ["Goto Passed"] },
  { id: "newflags", what: "UZDoom's gameplay flags set from DECORATE", props: NEW_FLAGS.map((f) => `+${f}`), states: allFlagsSet(NEW_FLAGS) },
  { id: "renderflips", what: "UZDoom's sprite flip render flags, set from DECORATE and by A_ChangeFlag", props: FLIP_FLAGS.map((f) => `+${f}`),
    states: ['TNT1 A 0 A_ChangeFlag("SPRITEFLIP", false)', 'TNT1 A 0 A_CheckFlag("SPRITEFLIP", "Failed")', 'TNT1 A 0 A_ChangeFlag("SPRITEFLIP", true)', ...allFlagsSet(FLIP_FLAGS)] },
  { id: "laxtelefrag", what: "+LAXTELEFRAGDMG lets DamageFactor cut telefrag damage", props: ["Health 1500000", "+SHOOTABLE", "PainChance 0", "DamageFactor 0.5", "+LAXTELEFRAGDMG"],
    states: ['TNT1 A 0 A_DamageSelf(1000000, "None", 0, "None", "None", AAPTR_DEFAULT, AAPTR_DEFAULT)', jumpIf("health == 1000000")] },
  { id: "forcezero", what: "+FORCEZERORADIUSDMG passes a zero-damage blast on (here as a forced pain)", verdictBy: "ZxZeroTarget", props: ["+FORCEZERORADIUSDMG", "+FORCEPAIN"],
    defs: actorDef("ZxZeroTarget", ["Health 100", "+SHOOTABLE", "+NOGRAVITY", "PainChance 256", "Radius 8", "Height 16"],
      ["TNT1 A -1", "Stop", "Pain:", ...verdictFrom("pass", "forcezero")]),
    states: [spawn("ZxZeroTarget", 48, 0, 0), "TNT1 A 1", 'TNT1 A 0 A_Explode(0, 128, 0)', "TNT1 A -1", "Stop"] },
  // [Doruk-Aksoy] The missile starts 64 east flying back west, and is gone before it could reach the next check.
  // [Doruk-Aksoy] DOHARMSPECIES, since a missile's own shooter is of its species and both engines' species rule would spare it.
  { id: "hitowner", what: "+HITOWNER missiles hit the one who fired them", props: ["Health 100", "+SHOOTABLE", "+SOLID", "+DOHARMSPECIES", "PainChance 0", "Radius 16", "Height 56"],
    defs: actorDef("ZxOwnMissile", ["Projectile", "+HITOWNER", "Radius 4", "Height 4", "Speed 6", "Damage 5"], ["TNT1 A 15", "Stop"]),
    states: ['TNT1 A 0 A_SpawnItemEx("ZxOwnMissile", 64, 0, 8, -6, 0, 0, 0, SXF_NOCHECKPOSITION)', "TNT1 A 20", jumpIf("health < 100")] },
  { id: "stoprails", what: "+STOPRAILS takes the rail and stops it", defs: actorDef("ZxRailStopper", ["Health 100", "+SHOOTABLE", "+NOGRAVITY", "+STOPRAILS", "PainChance 0", "Radius 8", "Height 56"])
      + actorDef("ZxRailBehind", ["Health 100", "+SHOOTABLE", "+NOGRAVITY", "PainChance 0", "Radius 8", "Height 56"]),
    states: [spawn("ZxRailStopper", 48, 0, 0, "SXF_NOCHECKPOSITION|SXF_ISMASTER"), spawn("ZxRailBehind", 96, 0, 0, "SXF_NOCHECKPOSITION|SXF_ISTARGET"), "TNT1 A 1",
      'TNT1 A 0 A_CustomRailgun(10, 0, "ff 00 00", "ff 00 00", RGF_SILENT, 0, 0, "BulletPuff", 0, 0, 128)', "TNT1 A 2",
      'TNT1 A 0 A_JumpIfHealthLower(100, "Failed", AAPTR_TARGET)', 'TNT1 A 0 A_JumpIfHealthLower(100, "Passed", AAPTR_MASTER)'] },
  // [Doruk-Aksoy] The victim goes to See only when it turns on the check, so its See state is the verdict; the check is a monster, so the victim does not take it as target when spawned.
  { id: "noinfightspecies", what: "+NOINFIGHTSPECIES never turns on its own species", props: ["+ISMONSTER", "+SHOOTABLE", 'Species "ZxSpA"'],
    defs: actorDef("ZxSpeciesVictim", ["+ISMONSTER", "+SHOOTABLE", "+NOGRAVITY", "Health 1000", "PainChance 0", 'Species "ZxSpA"', "+NOINFIGHTSPECIES"],
      ["TNT1 A -1", "Stop", "See:", ...verdictFrom("fail", "noinfightspecies")]),
    states: [spawn("ZxSpeciesVictim", 48, 0, 0, "SXF_NOCHECKPOSITION|SXF_ISTARGET"), "TNT1 A 1", "TNT1 A 0 A_DamageTarget(5)", "TNT1 A 10", "Goto Passed"] },
  { id: "forceinfighting", what: "+FORCEINFIGHTING turns on its attacker through NOINFIGHTING", verdictBy: "ZxForceVictim", props: ["+ISMONSTER", "+SHOOTABLE"],
    defs: actorDef("ZxForceVictim", ["+ISMONSTER", "+SHOOTABLE", "+NOGRAVITY", "Health 1000", "PainChance 0", "+NOINFIGHTING", "+FORCEINFIGHTING"],
      ["TNT1 A -1", "Stop", "See:", ...verdictFrom("pass", "forceinfighting")]),
    states: [spawn("ZxForceVictim", 48, 0, 0, "SXF_NOCHECKPOSITION|SXF_ISTARGET"), "TNT1 A 1", "TNT1 A 0 A_DamageTarget(5)", "TNT1 A -1", "Stop"] },
  { id: "smashable", what: "+SMASHABLE dies landing", verdictBy: "ZxSmash",
    defs: actorDef("ZxSmash", ["Health 10", "+SHOOTABLE", "+SMASHABLE", "Radius 8", "Height 8"], ["TNT1 A -1", "Stop", "Death:", ...verdictFrom("pass", "smashable")]),
    states: [spawn("ZxSmash", 0, 0, 64), "TNT1 A -1", "Stop"] },
  { id: "noautooffskullfly", what: "+NOAUTOOFFSKULLFLY keeps a charge on at rest", props: ["+SKULLFLY", "+NOAUTOOFFSKULLFLY"],
    states: ["TNT1 A 3", 'TNT1 A 0 A_CheckFlag("SKULLFLY", "Passed")'] },
  // [Doruk-Aksoy] A user variable tells the second pass through Spawn, where a slam that goes idle lands, from the first; the charge starts with its velocity, since one at rest turns itself off.
  { id: "retargetslam", what: "+RETARGETAFTERSLAM goes idle, not to See, after a slam", vars: ["var int user_slammed;"], defs: solidBox("ZxSlamWall"),
    props: ["+SOLID", "+RETARGETAFTERSLAM", "Radius 8", "Height 16"],
    states: ["TNT1 A 0 A_JumpIf(user_slammed == 1, \"Passed\")", 'TNT1 A 0 A_SetUserVar("user_slammed", 1)', spawn("ZxSlamWall", 48, 0, 0),
      'TNT1 A 0 A_ChangeFlag("SKULLFLY", true)', "TNT1 A 0 A_ChangeVelocity(8, 0, 0, CVF_REPLACE|CVF_RELATIVE)", "TNT1 A 20", "Goto Failed", "See:", "Goto Failed"] },
  { id: "slamstate", what: "a charger's own Slam state after a slam", props: ["+SOLID", "Radius 8", "Height 16"], defs: solidBox("ZxSlamWall2"),
    states: [spawn("ZxSlamWall2", 48, 0, 0), 'TNT1 A 0 A_ChangeFlag("SKULLFLY", true)', "TNT1 A 0 A_ChangeVelocity(8, 0, 0, CVF_REPLACE|CVF_RELATIVE)", "TNT1 A 20",
      "Goto Failed", "Slam:", "Goto Passed"] },
  { id: "onlyslamsolid", what: "+ONLYSLAMSOLID charges through what is neither solid nor shootable", props: ["+SOLID", "+ONLYSLAMSOLID", "Radius 8", "Height 16"],
    defs: actorDef("ZxSlamPickup", ["+SPECIAL", "+NOGRAVITY", "Radius 16", "Height 16"]),
    states: [pointAt("ZxSlamPickup", 40, 0, 0), 'TNT1 A 0 A_ChangeFlag("SKULLFLY", true)', "TNT1 A 0 A_ChangeVelocity(8, 0, 0, CVF_REPLACE|CVF_RELATIVE)", "TNT1 A 16", jumpIf("GetDistance(false) > 60")] },
  // [Doruk-Aksoy] Only a trail whose target is the check gives a verdict, since one may spawn before the projectile has its shooter.
  { id: "getowner", what: "+GETOWNER gives a fast projectile's trail its shooter as target", verdictBy: "ZxOwnerTrail",
    defs: actorDef("ZxFastShot : FastProjectile", ["Radius 2", "Height 2", "Speed 30", "Damage 0", 'MissileType "ZxOwnerTrail"', "+GETOWNER"], ["TNT1 A 3", "Stop"])
      + actorDef("ZxOwnerTrail", ["+NOINTERACTION"], ["TNT1 A 1", 'TNT1 A 0 A_JumpIf(CheckClass("ZxChk_getowner", AAPTR_TARGET), "Yes")', "Stop",
        "Yes:", ...verdictFrom("pass", "getowner")]),
    states: ['TNT1 A 0 A_CustomMissile("ZxFastShot", 8, 0, 0, CMF_AIMDIRECTION)', "TNT1 A -1", "Stop"] },
  // [Doruk-Aksoy] The bouncers outlive a check only by dying (Death) or by a state that checks them; one that just bounces runs out and leaves no marker.
  { id: "nobounceshootable", what: "+DONTBOUNCEONSHOOTABLES explodes on a shootable actor", verdictBy: "ZxNoShootBounce",
    defs: actorDef("ZxNoShootBounce", ["Projectile", 'BounceType "Doom"', "+DONTBOUNCEONSHOOTABLES", "Radius 4", "Height 4", "Damage 1"], ["TNT1 A 20", "Stop", "Death:", ...verdictFrom("pass", "nobounceshootable")])
      + actorDef("ZxBounceTarget", ["+SHOOTABLE", "+SOLID", "+NOGRAVITY", "Health 1000", "PainChance 0", "Radius 16", "Height 56"]),
    states: [spawn("ZxBounceTarget", 64, 0, 0), 'TNT1 A 0 A_SpawnItemEx("ZxNoShootBounce", 16, 0, 16, 8, 0, 0, 0, SXF_NOCHECKPOSITION)', "TNT1 A -1", "Stop"] },
  { id: "keepbounceangle", what: "+KEEPBOUNCEANGLE keeps its yaw bouncing off an actor", verdictBy: "ZxKeepAngle",
    defs: actorDef("ZxKeepAngle", ["Projectile", 'BounceType "Doom"', "+KEEPBOUNCEANGLE", "Radius 4", "Height 4", "Damage 0"],
      ["TNT1 A 8", 'TNT1 A 0 A_JumpIf(abs(angle) < 1 || angle > 359, "Yes")', "Stop", "Yes:", ...verdictFrom("pass", "keepbounceangle")]) + solidBox("ZxKeepBox"),
    states: [spawn("ZxKeepBox", 48, 0, 0), 'TNT1 A 0 A_SpawnItemEx("ZxKeepAngle", 8, 0, 16, 8, 0, 0, 0, SXF_NOCHECKPOSITION)', "TNT1 A -1", "Stop"] },
  // [Doruk-Aksoy] UZDoom's pitch is signed (up negative) and ours reads as an angle, so either form of "up" passes.
  { id: "bouncepitch", what: "+BOUNCEMODIFIESPITCH points along its velocity after a floor bounce", verdictBy: "ZxPitchBounce",
    defs: actorDef("ZxPitchBounce", ["Projectile", 'BounceType "Hexen"', "+BOUNCEMODIFIESPITCH", "BounceFactor 1", "Radius 4", "Height 4", "Damage 0"],
      ["TNT1 A 14", 'TNT1 A 0 A_JumpIf(pitch < -10 || (pitch > 270 && pitch < 350), "Yes")', "Stop", "Yes:", ...verdictFrom("pass", "bouncepitch")]),
    states: ['TNT1 A 0 A_SpawnItemEx("ZxPitchBounce", 16, 0, 32, 4, 0, -4, 0, SXF_NOCHECKPOSITION)', "TNT1 A -1", "Stop"] },
  // [Doruk-Aksoy] Not a missile, since ours already removes a bouncing missile on a sky floor, so an MBF bouncer (Grenade), the one kind of non-missile both engines bounce; the check holds it as its target and sees it gone.
  { id: "nobouncesky", what: "+DONTBOUNCEONSKY vanishes on a sky floor", sky: true,
    defs: actorDef("ZxSkyBouncer", ['BounceType "Grenade"', "+DONTBOUNCEONSKY", "BounceFactor 1", "Radius 4", "Height 4"]),
    states: [spawn("ZxSkyBouncer", 80, 0, 48, "SXF_NOCHECKPOSITION|SXF_ISTARGET"), "TNT1 A 35", jumpIf("IsPointerEqual(AAPTR_TARGET, AAPTR_NULL)")] },
  { id: "transnone", what: 'A_SetTranslation("None") clears the translation', props: ['Translation "Ice"'], translation: "none",
    states: ['TNT1 A 0 A_SetTranslation("None")', "Goto Passed"] },
  { id: "transreset", what: "A_fua_ResetTranslation restores the class's", props: ['Translation "Ice"'], translation: "class", fua: true,
    defs: actorDef("ZxTransTwin", ['Translation "Ice"']),
    states: [spawn("ZxTransTwin", 0, 32, 0), 'TNT1 A 0 A_SetTranslation("None")', "TNT1 A 0 A_fua_ResetTranslation", "Goto Passed"] },
];

export const checkClass = (c) => `ZxChk_${c.id}`;
export const checksFor = (withFua = true) => CHECKS.filter((c) => withFua || !c.fua);
const markerState = (kind, id) => [`TNT1 A 0 A_Log("zxcheck ${id} ${kind.toLowerCase()}")`, spawn(`Zx${kind}_${id}`, 0, 0, 0), "TNT1 A -1", "Stop"];

// [Doruk-Aksoy] Whether a state line ends a sequence, so nothing may follow it before a label.
const endsSequence = (st) => /^(Stop|Loop|Wait|Fail|Goto\b)/.test(st);

// [Doruk-Aksoy] A check's actor: two tics to let the map settle, its states, then Failed unless they reached Passed ("Fail" is a DECORATE keyword, so no label is named it).
export function checkDecorate(c) {
  const verdict = [...(c.verdictBy ? [] : ["Passed:", ...markerState("Pass", c.id)]), "Failed:", ...markerState("Fail", c.id)];
  const fallThrough = endsSequence(c.states[c.states.length - 1]) ? [] : ["Goto Failed"];
  return actorDef(checkClass(c), [...(c.vars || []), "+NOGRAVITY", ...(c.props || [])], ["TNT1 A 2", ...c.states, ...fallThrough, ...verdict], FIRST_NUM + CHECKS.indexOf(c));
}

// [Doruk-Aksoy] Actors script 1 spawns once the clients are in, so their changes go out live rather than with the join: each one's class, what it does, and what a client must then hold.
export const LIVE = [
  { cls: "ZxLiveNone", props: ['Translation "Ice"'], states: ["TNT1 A 2", 'TNT1 A 0 A_SetTranslation("None")'], expect: (r) => r.tr === 0, what: "translation 0" },
  { cls: "ZxLiveReset", props: ['Translation "Ice"'], fua: true, what: "the class's translation back",
    states: ["TNT1 A 2", 'TNT1 A 0 A_SetTranslation("None")', "TNT1 A 10", "TNT1 A 0 A_fua_ResetTranslation"], expect: (r) => r.tr !== 0 },
  { cls: "ZxLiveScale", states: ["TNT1 A 2", "TNT1 A 0 A_SetScale(2, 0, AAPTR_DEFAULT, true)"], expect: (r) => r.sx === 2 && r.sy === 0, what: "scale 2x0" },
];
export const LIVE_SPOT = 50, LIVE_SCRIPT = 1;
export const liveFor = (withFua = true) => LIVE.filter((l) => withFua || !l.fua);

// [Doruk-Aksoy] Every check's actor, its extra actors and both markers, and the live actors; withFua false leaves out what only ours has, for UZDoom.
export function buildDecorate({ withFua = true } = {}) {
  return checksFor(withFua).map((c) => (c.defs || "") + dummy(`ZxPass_${c.id}`) + dummy(`ZxFail_${c.id}`) + checkDecorate(c)).join("")
    + liveFor(withFua).map((l) => actorDef(l.cls, ["+NOGRAVITY", ...(l.props || [])], [...l.states, "TNT1 A -1", "Stop"])).join("");
}

// [Doruk-Aksoy] Where check i stands, on a grid across the room, each facing east.
export function checkSpot(i) {
  const perRow = Math.floor((ROOM_SIZE - SPACING) / SPACING);
  return { x: SPACING + (i % perRow) * SPACING, y: SPACING + 256 + Math.floor(i / perRow) * SPACING };
}

// [Doruk-Aksoy] The ACS the uservaracs check calls (GetUserVariable of user_f and user_i, SetUserVariable of user_h to 2.5), and script 1 spawning the live actors.
export function behavior({ withFua = true } = {}) {
  const strings = [...STRINGS, ...liveFor(withFua).map((l) => l.cls)];
  const get = (name) => [...callFunc(ACSF_GET_USER_VAR, [0, strings.indexOf(name)]), PCD.SETRESULTVALUE, PCD.TERMINATE];
  const spawnLive = liveFor(withFua).flatMap((l) => [...callFunc(ACSF.SpawnSpotForced, [strings.indexOf(l.cls), LIVE_SPOT, 0, 0]), PCD.DROP]);
  return acsScripts([
    { code: get("user_f"), type: SCRIPT.CLOSED, name: "zxgetfloat" },
    { code: get("user_i"), type: SCRIPT.CLOSED, name: "zxgetint" },
    { code: [...callFunc(ACSF_SET_USER_VAR, [0, strings.indexOf("user_h"), 0x28000]), PCD.DROP, PCD.TERMINATE], type: SCRIPT.CLOSED, name: "zxsetfloat" },
    { code: [...spawnLive, PCD.TERMINATE], type: SCRIPT.CLOSED, number: LIVE_SCRIPT },
  ], strings);
}

export function buildMap({ withFua = true } = {}) {
  const m = new UdmfMap();
  m.room(0, 0, ROOM_SIZE, ROOM_SIZE, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const [type, x] of [[1, 128], [2, 256], [3, 384], [4, 512]]) m.thing({ x, y: 64, angle: 90, type });
  checksFor(withFua).forEach((c, i) => {
    const { x, y } = checkSpot(i);
    // [Doruk-Aksoy] The zat check has a 32-high step 64 to 128 east of it.
    if (c.step) m.block(x + 64, y - 32, x + 128, y + 32, { ...ROOM, heightfloor: 32 }, 0);
    // [Doruk-Aksoy] The nobouncesky check has a sky floor 48 to 112 east of it.
    if (c.sky) m.block(x + 48, y - 32, x + 112, y + 32, { ...ROOM, texturefloor: "F_SKY1" }, 0);
    m.thing({ x, y, angle: 0, type: FIRST_NUM + CHECKS.indexOf(c) });
  });
  m.thing({ x: 1024, y: 128, angle: 0, type: THING.MapSpot, id: LIVE_SPOT });
  return m;
}

export function buildDecorateUzdoomPk3({ withFua = true } = {}) {
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap({ withFua }), { behavior: behavior({ withFua }) })) },
    { name: "decorate.txt", data: buildDecorate({ withFua }) }]);
}

// [Doruk-Aksoy] What is wrong with one world's actors: a check with no marker or a fail marker, and the translation and scale checks read off their rows.
export function judgeWorld(actors, { withFua = true } = {}) {
  const row = (cls) => actors.find((a) => a.c === cls);
  const faults = [];
  for (const c of checksFor(withFua)) {
    if (row(`ZxFail_${c.id}`)) faults.push(`${c.id} (${c.what}) failed`);
    else if (!row(`ZxPass_${c.id}`)) faults.push(`${c.id} (${c.what}) left no marker`);
    if (!c.translation && !c.scale) continue;
    // [Doruk-Aksoy] world.actors names a row's translation tr and its scale sx, sy; a field it lacks reads as a fault, not a pass.
    const r = row(checkClass(c));
    if (!r) {
      faults.push(`${c.id}: no row for ${checkClass(c)}`);
      continue;
    }
    if (c.translation === "none" && r.tr !== 0) faults.push(`${c.id}: translation ${r.tr}, not 0`);
    if (c.translation === "class") {
      const twin = row("ZxTransTwin");
      if (!twin) faults.push(`${c.id}: no twin to compare with`);
      else if (r.tr === undefined || r.tr !== twin.tr) faults.push(`${c.id}: translation ${r.tr}, not the class's ${twin.tr}`);
    }
    const same = (a, b) => Math.abs(a - b) <= 1e-6;
    if (c.scale && !(same(r.sx, c.scale[0]) && same(r.sy, c.scale[1]))) faults.push(`${c.id}: scale ${r.sx}x${r.sy}, not ${c.scale.join("x")}`);
  }
  return faults;
}

// [Doruk-Aksoy] Every actor of one instance, those of no network id too, since a single player gives none.
async function worldOf(c) {
  return (await c.rpc("world.actors", { limit: 100000, all: true }))?.actors || [];
}

// [Doruk-Aksoy] The checks finish within a few tics of the map's start.
export const SETTLE_TICS = 70;

// [Doruk-Aksoy] What is wrong with the live actors in one world, and with each against the server's when a server's world is given.
export function judgeLive(actors, serverActors = null, { withFua = true } = {}) {
  const faults = [];
  for (const l of liveFor(withFua)) {
    const r = actors.find((a) => a.c === l.cls);
    if (!r) { faults.push(`${l.cls}: not held`); continue; }
    if (!l.expect(r)) faults.push(`${l.cls}: translation ${r.tr}, scale ${r.sx}x${r.sy}, not ${l.what}`);
    const s = serverActors && serverActors.find((a) => a.c === l.cls);
    if (serverActors && (!s || s.tr !== r.tr || s.sx !== r.sx || s.sy !== r.sy)) faults.push(`${l.cls}: unlike the server's`);
  }
  return faults;
}

// [Doruk-Aksoy] Script 1's actors finish within a second of being spawned.
async function spawnLive(server) {
  await cap.exec(server, `puke ${LIVE_SCRIPT}`);
  await cap.waitTics(server, SETTLE_TICS);
}

const verdict = (label, faults, pass) => (faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${pass}`);

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, SETTLE_TICS);
  const faults = judgeWorld(await worldOf(c));
  await spawnLive(c);
  faults.push(...judgeLive(await worldOf(c)));
  return { line: verdict(label, faults, `${CHECKS.length} checks and ${LIVE.length} live changes pass offline`), failed: faults.length ? 1 : 0 };
}

// [Doruk-Aksoy] Each instance holds every check's result from the join, then each client holds what the server changed live.
export async function judgeNetplay(set, label) {
  const sc = set.server.c;
  await cap.waitTics(sc, SETTLE_TICS);
  const instances = [["server", sc], ...set.clients.map((cl) => [`${cl.role} player ${cl.player}`, cl.c])];
  const joined = [];
  for (const [name, c] of instances) joined.push([name, judgeWorld(await worldOf(c))]);
  await spawnLive(sc);
  for (const cl of set.clients) await cap.waitTics(cl.c, 10);
  const serverWorld = await worldOf(sc);
  const lines = [];
  let failed = 0;
  for (const [i, [name, c]] of instances.entries()) {
    const faults = [...joined[i][1], ...judgeLive(await worldOf(c), i === 0 ? null : serverWorld)];
    lines.push(verdict(`${label} ${name}`, faults, `${CHECKS.length} checks and ${LIVE.length} live changes pass`));
    if (faults.length) failed++;
  }
  return { lines, failed };
}

// [Doruk-Aksoy] Monsters kept offline, since the infighting checks are monsters (none of them moves or attacks).
export const SIDES = [{ name: "vk", launchCvars: { sv_nomonsters: 0 } }];
// [Doruk-Aksoy] The legacy netcode and the snapshot overhaul.
export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "snap", players: 1, observers: 1, serverCvars: SNAP_SERVER_CVARS }];

// [Doruk-Aksoy] Offline first, then hosted with a player and an observer.
export async function compareOfflineAndOnline(opts, { offline = compareEachLaunch, online = compareNetplay } = {}) {
  const a = await offline(opts);
  const b = await online(opts);
  return { lines: [...a.lines, ...b.lines], failed: a.failed + b.failed };
}
compareOfflineAndOnline.judgedAlone = true;

// [Doruk-Aksoy] What is wrong with UZDoom's log of the checks it shares with ours, each check logging its verdict.
export function judgeUzdoomLog(log) {
  const results = new Map([...String(log).matchAll(/zxcheck (\w+) (pass|fail)/g)].map((m) => [m[1], m[2]]));
  return checksFor(false).flatMap((c) => {
    const v = results.get(c.id);
    return v === "pass" ? [] : [`${c.id} (${c.what}) ${v ? "failed" : "logged nothing"}`];
  });
}

// [Doruk-Aksoy] The shared checks in UZDoom, judged by its log since no world rpc reaches it.
export function runInUzdoom({ dir = fs.mkdtempSync(path.join(os.tmpdir(), "decorateuzdoom-uz-")), run = runUzdoom } = {}) {
  const file = path.join(dir, "decorateuzdoom-uz.pk3");
  fs.writeFileSync(file, buildDecorateUzdoomPk3({ withFua: false }));
  const faults = judgeUzdoomLog(run({ pk3: file, map: "MAP01", chain: `wait ${SETTLE_TICS};quit`, dir: path.join(dir, "run"), timeoutMs: 90000 }));
  return { line: verdict("uzdoom", faults, `${checksFor(false).length} shared checks pass in UZDoom`), failed: faults.length ? 1 : 0 };
}

if (process.argv.includes("--uzdoom") && isMain(import.meta.url)) {
  const r = runInUzdoom();
  console.log(r.line);
  process.exit(r.failed ? 1 : 0);
} else {
  runScenarioIfMain(import.meta.url, () => ({ name: "decorateuzdoom", wad: buildDecorateUzdoomPk3(), maps: ["MAP01"], sides: SIDES, configs: CONFIGS,
    judgeLaunch, judgeNetplay }), compareOfflineAndOnline);
}

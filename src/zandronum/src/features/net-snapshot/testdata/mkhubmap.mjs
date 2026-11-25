// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] hubmap.wad: the three maps the pre-merge route needs, which no stock game
// carries -- Freedoom has no polyobject, no timed door on a hub map, and nothing that routes.
//
//   node src/zandronum/src/features/net-snapshot/testdata/mkhubmap.mjs
//
// MAP01 and MAP02 in one hub cluster, so leaving a map saves it and returning restores it from
// that snapshot rather than loading it fresh. Two things are under test.
//
// The hub-return specialdata line (po_man.cpp, DPolyAction::Serialize). MAP01 holds polyobject 1
// turning for ever, started by a walkover line a step in front of the player start, and a switch
// on the east wall running Polyobj_Move on the same polyobject -- the plain special, not the OR_
// override, so EV_MovePoly refuses while the rotation holds the polyobject's specialdata. Leave
// for MAP02, come back, and the refusal must still happen: the restored DRotatePoly puts itself
// back in specialdata on load, and without that line the polyobject reads as idle and the switch
// starts a second action on one already turning.
//
// The timed doors over a map change (p_doors.cpp, P_SpawnDoorCloseIn30 and
// P_SpawnDoorRaiseIn5Mins). MAP02 holds a door sector that closes 30 seconds in, tagged 10, and
// one that opens 5 minutes in, tagged 14. P_SpawnSpecials makes both at map load, which on the
// second map of a session is after the client already knows the mover gate, so a gated client must
// build neither and take both from their records.
//
// The special numbers are the ZDoom ones, 74 and 78: a `zdoom` namespace TEXTMAP is not translated
// (p_udmf.cpp's ParseSector translates a special only for a namespace it marks translated), so the
// familiar Doom-format 10 and 14 would mean nothing here. The tags are 10 and 14, which is where
// those two numbers belong in this map. The polyobject's lines are drawn around their anchor in a
// closed room east of the hall and moved to the start spot by PO_Init, the usual construction area.
//
// Run MAP01 and MAP02 in a session that is not deathmatch: deathmatch forces FINISH_NoHub
// (G_DoCompleted), which drops the snapshot and makes the return a fresh load.
//
// MAP03 is session 3's, in a cluster of its own so the hub never reaches it. A spawner in plain
// view of the start makes one of each cosmetic fixture a second, from a DECORATE state loop so
// the content is the same on every build and nothing about it is random:
//
//   - FuaCosHorn, for the SoundActor guard. Its own A_PlaySound with looping true takes the
//     branch of A_PlaySound, which calls SERVERCOMMANDS_SoundActor itself instead
//     of leaving the send to S_Sound, so a NetID of 0 puts CosmeticMutesSound in charge of it.
//   - FuaCosPebble, for the EnsureActorHasNetID guard. It is a bouncer aimed at FuaCosDrum, and
//     P_BounceActor's server send is one of the twelve commands with no
//     SuppressesLegacy check ahead of its EnsureActorHasNetID. The drum is neither a player nor
//     a monster, which is the clause in P_BounceActor that lets any bouncer bounce off it.
//
// Both only route once fua_snap_cosmetic_force names them, and only on a build whose
// FState::CallAction sets g_pSnapActionSelf: without it the spawner is unknown and
// NoteCosmeticSpawn drops every spawn before it reaches a row.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps } from "../../../../../../tools/fuactl/src/udmf.mjs";

const POLYOBJ_STARTLINE = 1;     // po_man.cpp: read by PO_Init, never run (po, mirror, sound)
const POLYOBJ_ROTATELEFT = 2;    // LS_Polyobj_RotateLeft (po, speed, byteangle); 255 turns for ever
const POLYOBJ_MOVE = 4;          // LS_Polyobj_Move (po, speed, byteangle, distance), no override
const TELEPORT_NEWMAP = 74;      // (levelnum, position, keepfacing)
const EXIT_NORMAL = 243;
const POLY_ANCHOR = 9300;        // mapinfo/common.txt's doomednums; the poly number is the angle
const POLY_SPAWN = 9301;         // ditto, and the non-crushing one (SMT_PolySpawn)
const SECTOR_DOORCLOSEIN30 = 74; // p_lnspec.h's dSector_DoorCloseIn30
const SECTOR_DOORRAISEIN5MINS = 78; // p_lnspec.h's dSector_DoorRaiseIn5Mins
const POLY = 1;                  // the polyobject number the things and both specials name
const CLUSTER = 90;              // a number no stock mapinfo uses; Doom 2 fills 5 to 11
const CLUSTER3 = 91;             // MAP03's own, so the hub never reaches it
const SPAWNER = 30000;           // the doomednums block below gives these to the two classes
const DRUM = 30001;

const TEX = { texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const SWITCH = { texturemiddle: "SW1STRTN" };
const OPEN = { texturemiddle: "-" };
const DOORFRAME = { texturemiddle: "-", texturetop: "BIGDOOR2" };
const FLAT = { heightfloor: 0, heightceiling: 128, ...TEX };

// ---------------------------------------------------------------- MAP01: the rotator

const m1 = new UdmfMap({ namespace: "zdoom" });

// The hall, with the polyobject turning at its centre. Its east wall is the switch under test,
// repeatable so the refusal can be tried as often as the route asks; its north wall is the exit.
const hall = m1.room(-384, -256, 384, 256, FLAT, (edge) => ({
  side: edge === "east" || edge === "north" ? SWITCH : WALL,
  line: edge === "east"
    ? { special: POLYOBJ_MOVE, arg0: POLY, arg1: 8, arg2: 64, arg3: 128,
        playeruse: true, repeatspecial: true }
    : edge === "north" ? { special: EXIT_NORMAL, arg0: 0, playeruse: true } : {},
}));

// The construction area: a closed room holding the polyobject's own lines, which PO_Init moves
// out of it, leaving an empty box no player can reach.
const stage = m1.room(768, -128, 960, 128, FLAT, () => ({ side: WALL }));

// The polyobject, a 64 unit square wound anticlockwise so each one-sided line faces outward, as a
// solid a player walks around must be. One line carries Polyobj_StartLine; SpawnPolyobj finds the
// other three by following the vertex chain from it (po_man.cpp, IterFindPolySides).
const square = [[832, -32, 896, -32], [896, -32, 896, 32], [896, 32, 832, 32], [832, 32, 832, -32]];
square.forEach(([x1, y1, x2, y2], i) => {
  m1.wall(x1, y1, x2, y2, stage, WALL,
    i === 0 ? { special: POLYOBJ_STARTLINE, arg0: POLY, arg1: 0, arg2: 0 } : {});
});

// The anchor at the square's own centre and the start spot at the hall's: TranslateToStartSpot
// subtracts the one from the other, so the square lands on the spot and turns about it.
m1.thing({ x: 864, y: 0, angle: POLY, type: POLY_ANCHOR });
m1.thing({ x: 0, y: 0, angle: POLY, type: POLY_SPAWN });

// The player, facing north a step short of the line that starts the rotation.
m1.thing({ x: -320, y: -160, angle: 90, type: 1 });
m1.opening(-352, -128, -288, -128, hall, hall, OPEN, OPEN,
  { special: POLYOBJ_ROTATELEFT, arg0: POLY, arg1: 8, arg2: 255,
    playercross: true, blocking: false });

// ---------------------------------------------------------------- MAP02: the timed doors

const m2 = new UdmfMap({ namespace: "zdoom" });

// The start room, whose south wall returns to MAP01 by levelnum, which is what makes the return a
// hub return rather than an ordinary exit.
const start = m2.sector({ ...FLAT });
// The door that closes 30 seconds in: open at load, so it is seen to shut.
const close30 = m2.sector({ ...FLAT, id: 10, special: SECTOR_DOORCLOSEIN30 });
// The door that opens 5 minutes in: shut at load, ceiling on the floor.
const open5 = m2.sector({ ...FLAT, heightceiling: 0, id: 14, special: SECTOR_DOORRAISEIN5MINS });
const beyond30 = m2.sector({ ...FLAT });
const beyond5 = m2.sector({ ...FLAT });

m2.wall(-640, -192, -640, 192, start, WALL);
m2.wall(-640, 192, -256, 192, start, WALL);
m2.wall(-256, 64, -256, -64, start, WALL);
m2.wall(-256, -192, -640, -192, start, SWITCH,
  { special: TELEPORT_NEWMAP, arg0: 1, arg1: 0, arg2: 0, playeruse: true });
m2.thing({ x: -448, y: 0, angle: 0, type: 1 });

// Each door is a corridor between the start room and a room beyond it, so a player can stand
// under it and watch it move. Wound so the named front sector lies on the line's right.
const corridor = (door, beyond, yLow, yHigh) => {
  m2.opening(-256, yHigh, -256, yLow, start, door, DOORFRAME, DOORFRAME, { dontpegtop: true });
  m2.wall(-256, yLow, 256, yLow, door, WALL);
  m2.wall(256, yHigh, -256, yHigh, door, WALL);
  m2.opening(256, yHigh, 256, yLow, door, beyond, DOORFRAME, DOORFRAME, { dontpegtop: true });
  m2.wall(256, yLow, 640, yLow, beyond, WALL);
  m2.wall(640, yLow, 640, yHigh, beyond, WALL);
  m2.wall(640, yHigh, 256, yHigh, beyond, WALL);
};
corridor(close30, beyond30, 64, 192);
corridor(open5, beyond5, -192, -64);

// ---------------------------------------------------------------- MAP03: the cosmetic guards

const m3 = new UdmfMap({ namespace: "zdoom" });

// One room, so the spawner, both fixtures and the drum are all in view of the start and a
// netcap window needs no walking about.
m3.room(-512, -384, 512, 384, { ...FLAT, heightceiling: 160 }, () => ({ side: WALL }));
m3.thing({ x: -416, y: 0, angle: 0, type: 1 });
m3.thing({ x: -288, y: 0, angle: 0, type: SPAWNER });
m3.thing({ x: 160, y: 0, angle: 0, type: DRUM });

// [Kaori-Chani] The fixtures, as plain as they can be and still reach their guard: the spawner
// is an ordinary server actor, and only what it spawns is meant to route.
const DECORATE = `// hubmap.wad, MAP03: one fixture per reachable cosmetic guard.
// Neither routes until the server is told to force them:
//   fua_snap_cosmetic_force "FuaCosHorn FuaCosPebble"

ACTOR FuaCosSpawner ${SPAWNER}
{
	Radius 16
	Height 32
	+NOGRAVITY
	+NOBLOCKMAP
	+DONTSPLASH
	States
	{
	Spawn:
		TNT1 A 35
		TNT1 A 0 A_SpawnItemEx("FuaCosHorn", 0, 0, 24, 0, 0, 0, 0, SXF_NOCHECKPOSITION)
		TNT1 A 0 A_SpawnItemEx("FuaCosPebble", 16, 0, 20, 12, 0, 0, 0,
			SXF_NOCHECKPOSITION|SXF_ABSOLUTEVELOCITY)
		Loop
	}
}

// Its looping A_PlaySound calls SERVERCOMMANDS_SoundActor directly, bypassing S_Sound's hook.
ACTOR FuaCosHorn
{
	Radius 8
	Height 8
	+NOGRAVITY
	+NOBLOCKMAP
	+NOINTERACTION
	States
	{
	Spawn:
		BAL1 A 2 BRIGHT
		BAL1 A 0 A_PlaySound("weapons/pistol", CHAN_BODY, 1.0, TRUE)
		BAL1 AB 14 BRIGHT
		Stop
	}
}

// A bouncer aimed at the drum; each strike is one PlayBounceSound send.
ACTOR FuaCosPebble
{
	Radius 6
	Height 6
	Speed 12
	Mass 10
	Damage 0
	BounceType "Doom"
	BounceCount 2
	BounceSound "world/barrelx"
	+MISSILE
	+NOGRAVITY
	+DONTSPLASH
	States
	{
	Spawn:
		BAL1 AB 4 BRIGHT
		Loop
	Death:
		BAL1 C 6 BRIGHT
		Stop
	}
}

// What it strikes: solid and shootable so a missile registers the hit, and no monster, which is
// what lets P_BounceActor bounce anything off it.
ACTOR FuaCosDrum ${DRUM}
{
	Radius 20
	Height 48
	Health 1000000
	+SOLID
	+SHOOTABLE
	+NOBLOOD
	+NOGRAVITY
	+DONTTHRUST
	States
	{
	Spawn:
		BAR1 A -1
		Stop
	}
}
`;

// ---------------------------------------------------------------- the wad

// [Kaori-Chani] One cluster, declared a hub, which is what saves each map as it is left; without
// the hub a return is a fresh load and the rotation would be gone rather than restored.
const MAPINFO = `// hubmap.wad, for session 4 of the pre-merge route.
map MAP01 "Hub Rotator"
{
	cluster = ${CLUSTER}
	next = "MAP02"
	sky1 = "SKY1"
	music = "D_RUNNIN"
}

map MAP02 "Timed Doors"
{
	cluster = ${CLUSTER}
	next = "MAP01"
	sky1 = "SKY1"
	music = "D_STALKS"
}

map MAP03 "Cosmetic Guards"
{
	cluster = ${CLUSTER3}
	next = "MAP03"
	sky1 = "SKY1"
	music = "D_COUNTD"
}

cluster ${CLUSTER}
{
	hub
}

doomednums
{
	${SPAWNER} = FuaCosSpawner
	${DRUM} = FuaCosDrum
}
`;

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), "hubmap.wad");
fs.writeFileSync(out, wad([
  { name: "MAPINFO", data: MAPINFO },
  { name: "DECORATE", data: DECORATE },
  ...mapLumps("MAP01", m1),
  ...mapLumps("MAP02", m2),
  ...mapLumps("MAP03", m3),
]));
console.log(`wrote ${out}: MAP01 hall ${hall} stage ${stage}, `
  + `MAP02 start ${start} close30 ${close30} open5 ${open5}, MAP03 one room`);

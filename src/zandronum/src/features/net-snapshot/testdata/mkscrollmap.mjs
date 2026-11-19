// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] scrollmap.wad: the smallest map carrying the scroller and pusher state the
// legacy DoScroller and DoPusher leave out, which no stock map does -- Freedoom has no
// control-sector or accelerative scroller anywhere, so nothing else exercises SnapSpecialState.
//
//   node src/zandronum/src/features/net-snapshot/testdata/mkscrollmap.mjs
//
// MAP34, a slot no stock game fills, so loading it proves it is this map. One room, with:
//   - a control-sector (displacement) floor scroller, args[1] & 1, whose last height is what
//     DoScroller cannot carry;
//   - an accelerative floor scroller, args[1] & 2, whose accumulated velocity is the other
//     missing field, and which only accumulates while its control sector moves;
//   - a ceiling scroller of each kind, so the census counts more than one of each;
//   - a point pusher, whose centre and radius a client cannot compute without its source.
// A walkover line starts the control sector moving, so the velocity is nonzero when a second
// client joins.

import fs from "node:fs";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../../../../../../tools/fuactl/src/udmf.mjs";

const SCROLL_FLOOR = 223;         // actionspecials.h: Scroll_Floor (tag, bits, type, x, y)
const SCROLL_CEILING = 224;       // Scroll_Ceiling, the same arguments
const POINTPUSH_SETFORCE = 227;   // PointPush_SetForce (tag, tid, magnitude, line)
const FLOOR_LOWERBYVALUE = 20;
const ELEVATOR_LOWERTONEAREST = 247;  // actionspecials.h: Elevator_LowerToNearest (tag, speed)
const SCROLL_DISPLACEMENT = 1;    // args[1] & 1: the control sector's movement scrolls it
const SCROLL_ACCELERATIVE = 2;    // args[1] & 2: and accumulates a velocity
const POINTPUSHER = 5001;         // the thing a point pusher takes its centre from
const PUSH_MASK = 0x1000;         // p_lnspec.h: without it on the sector, DPusher::Tick returns
const CEILING_CRUSHANDRAISE = 42;  // Ceiling_CrushAndRaise (tag, speed, crush, crushmode)
const CEILING_LOWERBYVALUE = 40;  // actionspecials.h: Ceiling_LowerByValue (tag, speed, height)
const DOOR_CLOSE = 10;            // actionspecials.h: Door_Close (tag, speed)
const FLOOR_WAGGLE = 138;         // Floor_Waggle (tag, amplitude, frequency, offset, timer)
const DOOR_CLOSE_IN_30 = 74;      // p_lnspec.h: a door spawned at map load, closing 30s in
const PLAT_DOWNWAITUPSTAY = 62;   // Plat_DownWaitUpStay (tag, speed, delay in tics)
const PLAT_PERPETUALRAISE = 60;   // actionspecials.h: Plat_PerpetualRaise (tag, speed, delay)

const TEX = { texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const OPEN = { texturemiddle: "-" };
const FLAT = { heightfloor: 0, heightceiling: 128, ...TEX };

const m = new UdmfMap({ namespace: "zdoom" });

// The room the player stands in, the two sectors that scroll, the control whose floor
// drives them, and a room east of it holding only a door. Tags: 1 control, 2 scrolled,
// 3 the pusher's sector, 4 the door's, which shares its ceiling with no other mover and
// spawns its own door at map load, so no line has to be reachable to start one.
const room = m.sector({ ...FLAT, heightfloor: -64, id: 3, special: PUSH_MASK });
const scrolled = m.sector({ ...FLAT, id: 2 });
const control = m.sector({ ...FLAT, id: 1 });
const doorsec = m.sector({ ...FLAT, id: 4, special: DOOR_CLOSE_IN_30 });
const slowdoor = m.sector({ ...FLAT, id: 5 });
// Tag 6, floor raised so FindLowestFloorSurrounding is below it: nothing else owns this
// floor, so the plat's wait is the only countdown on it.
const platsec = m.sector({ ...FLAT, heightfloor: 32, id: 6 });

const perimeter = [
  [-384, -128, -384, 128, room],
  [-384, 128, -128, 128, room],
  [-128, 128, 128, 128, scrolled],
  [128, 128, 384, 128, control],
  [384, 128, 640, 128, doorsec],
  [640, 128, 896, 128, slowdoor],
  [896, 128, 1152, 128, platsec],
  [1152, 128, 1152, -128, platsec],
  [1152, -128, 896, -128, platsec],
  [896, -128, 640, -128, slowdoor],
  [640, -128, 384, -128, doorsec],
  [384, -128, 128, -128, control],
  [128, -128, -128, -128, scrolled],
  [-128, -128, -384, -128, room],
];
for (const [x1, y1, x2, y2, s] of perimeter) m.wall(x1, y1, x2, y2, s, WALL, {});

m.opening(-128, -128, -128, 128, scrolled, room, OPEN, OPEN);
m.opening(128, -128, 128, 128, control, scrolled, OPEN, OPEN);
m.opening(384, -128, 384, 128, doorsec, control, OPEN, OPEN);
m.opening(640, -128, 640, 128, slowdoor, doorsec, OPEN, OPEN);
m.opening(896, -128, 896, 128, platsec, slowdoor, OPEN, OPEN);

// The four scrollers, all on sector tag 2, all driven by control sector tag 1. Each line's own
// side sector is the control, as P_SpawnScrollers reads it from sidedef[0].
const scrollers = [
  [SCROLL_FLOOR, SCROLL_DISPLACEMENT],
  [SCROLL_FLOOR, SCROLL_ACCELERATIVE],
  [SCROLL_CEILING, SCROLL_DISPLACEMENT],
  [SCROLL_CEILING, SCROLL_ACCELERATIVE],
];
let x = 160;
for (const [special, bits] of scrollers) {
  m.opening(x, -64, x, 64, control, control, OPEN, OPEN,
    { special, arg0: 2, arg1: bits, arg2: 0, arg3: 32, arg4: 0, blocking: false });
  x += 32;
}

// The point pusher, named by tid so its own sector needs no tag, and the thing it pushes from.
m.opening(-300, -64, -300, 64, room, room, OPEN, OPEN,
  { special: POINTPUSH_SETFORCE, arg0: 0, arg1: 7, arg2: 64, arg3: 0, blocking: false });
m.thing({ x: -200, y: 0, angle: 0, type: POINTPUSHER, id: 7 });

// A plat that never stops, so a mover is always mid-move: the reset's hold and release have
// something to hold. Behind the player's start, so only walking backwards begins it and a
// forward step can start the floor below with nothing else running.
m.opening(-372, -64, -372, 64, room, room, OPEN, OPEN,
  { special: PLAT_PERPETUALRAISE, arg0: 2, arg1: 8, arg2: 35, playercross: true,
    blocking: false });

// The first line a player meets, and repeatable: one step starts a floor that finishes
// inside a capture window, without the lift behind the start having been touched.
m.opening(-352, -64, -352, 64, room, room, OPEN, OPEN,
  { special: FLOOR_LOWERBYVALUE, arg0: 1, arg1: 2, arg2: 64, playercross: true,
    repeatspecial: true, blocking: false });

// An elevator on the scrolled sector, which moves both its planes together, so the gate's
// guard on that pair is exercised too. Also behind the start, and east of the lift's line.
m.opening(-366, -64, -366, 64, room, room, OPEN, OPEN,
  { special: ELEVATOR_LOWERTONEAREST, arg0: 2, arg1: 2, playercross: true,
    repeatspecial: true, blocking: false });

// A crusher on the control sector, whose ceiling nothing else owns: it reverses at the floor
// and again at the top, which is the reversal a gated client has to work out for itself.
m.opening(-224, -64, -224, 64, room, room, OPEN, OPEN,
  { special: CEILING_CRUSHANDRAISE, arg0: 1, arg1: 8, arg2: 10, arg3: 0,
    playercross: true, repeatspecial: true, blocking: false });

// The plat, on tag 6: down 32 units at speed 2, then a 70 tic wait, then up and gone.
// The wait is what a gated client counts down for itself.
m.opening(-192, -64, -192, 64, room, room, OPEN, OPEN,
  { special: PLAT_DOWNWAITUPSTAY, arg0: 6, arg1: 2, arg2: 70,
    playercross: true, repeatspecial: true, blocking: false });

// A ceiling on the scrolled sector, east of the floor's line and reachable with a forward
// step: 32 units at speed 1. Not the door's room, whose ceiling rests on its floor once shut.
m.opening(-320, -64, -320, 64, room, room, OPEN, OPEN,
  { special: CEILING_LOWERBYVALUE, arg0: 2, arg1: 1, arg2: 32, playercross: true,
    repeatspecial: true, blocking: false });

// A door slow enough to still be moving ten seconds later, which is what a split reset
// needs: 128 units at speed 1, in a room of its own so nothing else owns that ceiling.
m.opening(-288, -64, -288, 64, room, room, OPEN, OPEN,
  { special: DOOR_CLOSE, arg0: 5, arg1: 1, playercross: true, repeatspecial: true,
    blocking: false });

// A waggle on the door room's floor, which nothing else moves, timed so it reduces and
// removes itself: that write is what DWaggleBase::EndPlane now carries.
m.opening(-256, -64, -256, 64, room, room, OPEN, OPEN,
  { special: FLOOR_WAGGLE, arg0: 4, arg1: 32, arg2: 16, arg3: 0, arg4: 3,
    playercross: true, repeatspecial: true, blocking: false });

m.thing({ x: -360, y: 0, angle: 0, type: 1 });

const out = path.join(path.dirname(new URL(import.meta.url).pathname.slice(1)), "scrollmap.wad");
fs.writeFileSync(out, wad(mapLumps("MAP34", m)));
console.log(`wrote ${out}: room ${room}, scrolled ${scrolled}, control ${control}`);

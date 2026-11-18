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
const SCROLL_DISPLACEMENT = 1;    // args[1] & 1: the control sector's movement scrolls it
const SCROLL_ACCELERATIVE = 2;    // args[1] & 2: and accumulates a velocity
const POINTPUSHER = 5001;         // the thing a point pusher takes its centre from
const PUSH_MASK = 0x1000;         // p_lnspec.h: without it on the sector, DPusher::Tick returns
const PLAT_PERPETUALRAISE = 60;   // actionspecials.h: Plat_PerpetualRaise (tag, speed, delay)

const TEX = { texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const OPEN = { texturemiddle: "-" };
const FLAT = { heightfloor: 0, heightceiling: 128, ...TEX };

const m = new UdmfMap({ namespace: "zdoom" });

// The room the player stands in, the two sectors that scroll, and the control whose floor
// drives them. Tags: 1 control, 2 scrolled, 3 the pusher's sector.
const room = m.sector({ ...FLAT, heightfloor: -64, id: 3, special: PUSH_MASK });
const scrolled = m.sector({ ...FLAT, id: 2 });
const control = m.sector({ ...FLAT, id: 1 });

const perimeter = [
  [-384, -128, -384, 128, room],
  [-384, 128, -128, 128, room],
  [-128, 128, 128, 128, scrolled],
  [128, 128, 384, 128, control],
  [384, 128, 384, -128, control],
  [384, -128, 128, -128, control],
  [128, -128, -128, -128, scrolled],
  [-128, -128, -384, -128, room],
];
for (const [x1, y1, x2, y2, s] of perimeter) m.wall(x1, y1, x2, y2, s, WALL, {});

m.opening(-128, -128, -128, 128, scrolled, room, OPEN, OPEN);
m.opening(128, -128, 128, 128, control, scrolled, OPEN, OPEN);

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
// something to hold, and its one-second wait exercises the wait census too.
m.opening(-350, -64, -350, 64, room, room, OPEN, OPEN,
  { special: PLAT_PERPETUALRAISE, arg0: 2, arg1: 8, arg2: 35, playercross: true,
    blocking: false });

// Crossed on the way in, so the control sector is moving - and the accelerative scrollers are
// accumulating - by the time a second client joins.
m.opening(-340, -64, -340, 64, room, room, OPEN, OPEN,
  { special: FLOOR_LOWERBYVALUE, arg0: 1, arg1: 1, arg2: 256, playercross: true,
    blocking: false });

m.thing({ x: -360, y: 0, angle: 0, type: 1 });

const out = path.join(path.dirname(new URL(import.meta.url).pathname.slice(1)), "scrollmap.wad");
fs.writeFileSync(out, wad(mapLumps("MAP34", m)));
console.log(`wrote ${out}: room ${room}, scrolled ${scrolled}, control ${control}`);

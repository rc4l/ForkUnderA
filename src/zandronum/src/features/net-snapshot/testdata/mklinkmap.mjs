// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] linkmap.wad: the smallest map that shows a joiner a linked sector's floor, which
// no stock map does -- Freedoom carries no Sector_SetLink anywhere.
//
//   node src/zandronum/src/features/net-snapshot/testdata/mklinkmap.mjs
//
// MAP33, a slot no stock game fills, so loading it proves it is this map. Three rooms in a row:
// the west one holds the player start and a walkover line running Sector_SetLink (control tag 1,
// linked tag 2, floor, LINK_FLOORBITS), so the link exists once a player has crossed into the
// middle room. The middle room is the control, tag 1, with a repeatable switch on its east wall
// lowering its floor 64 units, for a hand test.
// The east room is linked, tag 2, and its floor must follow the control's.
//
// The east room's sector is allocated BEFORE the control's, so the control carries the higher
// index. That is what makes the fault visible: a joiner installs the link first (SetSectorLink,
// sent ahead of the heights), and each height it then receives drags the linked floor by that
// delta through P_MoveLinkedSectors. In index order the linked floor is set correctly and the
// control's height arrives afterwards and pulls it away; in link order the control lands first
// and the linked sector's own height is the last word on it.
//
// What to look for on a joiner that connects mid-move: the east room's floor level with the
// middle room's. Before the fix it sits out by the control's delta.

import fs from "node:fs";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "../../../../../../tools/fuactl/src/udmf.mjs";

const SECTOR_SETLINK = 51;      // p_lnspec.cpp: Sector_SetLink (controltag, linktag, floor, movetype)
const FLOOR_LOWERBYVALUE = 20;  // the switch's own special
const LINK_FLOORBITS = 5;       // p_linkedsectors.cpp

const TEX = { texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const OPEN = { texturemiddle: "-" };
const FLAT = { heightfloor: 0, heightceiling: 128, ...TEX };

const m = new UdmfMap({ namespace: "zdoom" });

// [Kaori-Chani] The three sectors, allocated west, linked, control, so the control carries the
// highest index of the three. Geometry is built by hand rather than with room(), which would put
// a solid wall across each doorway as well as the opening.
const west = m.sector(FLAT);
const linked = m.sector({ ...FLAT, id: 2 });
const control = m.sector({ ...FLAT, id: 1 });

// The perimeter, clockwise, each edge split where the sectors meet so every wall has one sector
// on its right.
const perimeter = [
  [-384, -128, -384, 128, west],   // west end
  [-384, 128, -128, 128, west],    // north, over the west room
  // The switch for a hand test rides this wall: press it facing north to lower tag 1 by 64.
  [-128, 128, 128, 128, control, { special: FLOOR_LOWERBYVALUE, arg0: 1, arg1: 8, arg2: 64,
    playeruse: true, repeatspecial: true }],
  [128, 128, 384, 128, linked],    // north, over the linked room
  [384, 128, 384, -128, linked],   // east end
  [384, -128, 128, -128, linked],  // south, under the linked room
  [128, -128, -128, -128, control],// south, under the control
  [-128, -128, -384, -128, west],  // south, under the west room
];
for (const [x1, y1, x2, y2, s, line] of perimeter) m.wall(x1, y1, x2, y2, s, WALL, line || {});

// The two doorways, wound so the sector named front lies on the line's right.
m.opening(-128, -128, -128, 128, control, west, OPEN, OPEN);
m.opening(128, -128, 128, 128, linked, control, OPEN, OPEN);

// Two triggers inside the west room, two-sided with both sides in it, crossed in order by a
// player who only walks forward: the first makes the link, the second starts a descent of 256 units, about a
// minute and a half, so a joiner has time to arrive part way down.
m.opening(-280, -64, -280, 64, west, west, OPEN, OPEN,
  { special: SECTOR_SETLINK, arg0: 1, arg1: 2, arg2: 0, arg3: LINK_FLOORBITS, playercross: true,
    blocking: false });
m.opening(-240, -64, -240, 64, west, west, OPEN, OPEN,
  { special: FLOOR_LOWERBYVALUE, arg0: 1, arg1: 1, arg2: 256, playercross: true, blocking: false });

m.thing({ x: -320, y: 0, angle: 0, type: 1 });

const out = path.join(path.dirname(new URL(import.meta.url).pathname.slice(1)), "linkmap.wad");
fs.writeFileSync(out, wad(mapLumps("MAP33", m)));
console.log(`wrote ${out}: west ${west}, linked ${linked}, control ${control}`);

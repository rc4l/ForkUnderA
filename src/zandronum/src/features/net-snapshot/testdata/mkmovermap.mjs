// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] movermap.wad: one room with a door, a lift and a crusher a player starts by
// walking forward, so the three sector-mover classes the gate covers all run at once.
//
//   node src/zandronum/src/features/net-snapshot/testdata/mkmovermap.mjs
//
// MAP35, a slot no stock game fills (Freedoom 2 has a MAP32), so loading it proves it is this map.
// The movers are blocks standing in the room rather than side alcoves: the lift and the crusher are
// perpetual and keep records flowing for as long as the map runs, while the door is one shot with a
// four second wait, on a repeatable walkover so crossing it again runs its countdown again.
//
// What this map is for: the mover hook figures in README.md, which need a client watching a mover
// of each class at once. Walk forward from the start and the plat trigger, the crusher trigger and
// the door trigger are crossed in that order, about a second apart.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps } from "../../../../../../tools/fuactl/src/udmf.mjs";

const DOOR_RAISE = 12;              // Door_Raise (tag, speed, delay, lighttag)
const CEILING_CRUSHANDRAISE = 42;   // Ceiling_CrushAndRaise (tag, speed, crush, crushtype)
const PLAT_PERPETUALRAISE = 60;     // Plat_PerpetualRaise (tag, speed, delay)

const TEX = { texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALL = { texturemiddle: "STARTAN2" };
const OPEN = { texturemiddle: "-" };
const FACE = { texturemiddle: "-", texturetop: "STARTAN2", texturebottom: "STARTAN2" };

const m = new UdmfMap({ namespace: "zdoom" });

const room = m.room(-512, -256, 512, 256,
  { heightfloor: 0, heightceiling: 256, ...TEX },
  () => ({ side: WALL }));

// The three movers, each a block in the room: the door shut (ceiling on its floor), the lift
// standing 64 above the floor it drops to, the crusher under a ceiling 192 up.
const door = m.block(-448, 96, -320, 224,
  { heightfloor: 0, heightceiling: 0, ...TEX, id: 1 }, room, () => FACE, () => FACE);
const lift = m.block(-64, 96, 64, 224,
  { heightfloor: 64, heightceiling: 256, ...TEX, id: 2 }, room, () => FACE, () => FACE);
const crusher = m.block(320, 96, 448, 224,
  { heightfloor: 0, heightceiling: 192, ...TEX, id: 3 }, room, () => FACE, () => FACE);

// The triggers, two-sided with both sides in the room and crossed in order by a player who only
// walks forward; the door's repeats, so its wait can be watched more than once.
m.opening(-256, -128, 256, -128, room, room, OPEN, OPEN,
  { special: PLAT_PERPETUALRAISE, arg0: 2, arg1: 8, arg2: 35, playercross: true, blocking: false });
m.opening(-256, -64, 256, -64, room, room, OPEN, OPEN,
  { special: CEILING_CRUSHANDRAISE, arg0: 3, arg1: 8, arg2: 10, playercross: true,
    blocking: false });
m.opening(-256, 0, 256, 0, room, room, OPEN, OPEN,
  { special: DOOR_RAISE, arg0: 1, arg1: 16, arg2: 140, playercross: true, repeatspecial: true,
    blocking: false });

m.thing({ x: 0, y: -208, angle: 90, type: 1 });

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), "movermap.wad");
fs.writeFileSync(out, wad(mapLumps("MAP35", m)));
console.log(`wrote ${out}: room ${room}, door ${door}, lift ${lift}, crusher ${crusher}`);

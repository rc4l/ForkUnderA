// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one declaration of what the renderer reads of the map, each line a member of a map structure's state (SectorState, SideState, LineState, VertexState in r_defs.h), from which come the copy's comparison and fill, fua_copy_check, the size check that keeps this list and the structs in step, and the boundary tripwire's patterns for reading a member live.

// [rc4l] ZX_COPY_FIELD(kind, member, group): group is what a change means to the renderer's caches, Shape (walls re-baked), Look (re-checked, re-baked only if the picture changed) or Plane (a plane's placement re-bakes its flats, anything else its walls), and the list has no include guard since each use expands it with ZX_COPY_FIELD defined.

ZX_COPY_FIELD(sector, planes, Plane)
ZX_COPY_FIELD(sector, floorplane, Shape)
ZX_COPY_FIELD(sector, ceilingplane, Shape)
ZX_COPY_FIELD(sector, ColorMap, Look)
ZX_COPY_FIELD(sector, special, Look)
ZX_COPY_FIELD(sector, lightlevel, Look)
ZX_COPY_FIELD(sector, MoreFlags, Look)
ZX_COPY_FIELD(sector, Flags, Look)
ZX_COPY_FIELD(sector, reflect, Look)
ZX_COPY_FIELD(side, textures, Shape)
ZX_COPY_FIELD(side, Light, Shape)
ZX_COPY_FIELD(side, Flags, Shape)
ZX_COPY_FIELD(line, flags, Shape)
ZX_COPY_FIELD(line, special, Shape)
ZX_COPY_FIELD(line, Alpha, Shape)
ZX_COPY_FIELD(line, args, Shape)
ZX_COPY_FIELD(vertex, x, Shape)
ZX_COPY_FIELD(vertex, y, Shape)

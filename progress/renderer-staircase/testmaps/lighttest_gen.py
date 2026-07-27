#!/usr/bin/env python3
# Build a minimal Hexen-format PWAD (MAP01) that exercises the 3D-light-splitting
# renderer path: one room, a solid 3D-floor slab crossing the walls midway, and a
# ShortRedTorch (DoomEdNum 57 -> dynamic light via Zandronum's doomdefs.txt) on the
# floor lighting walls that the slab splits into two light regions.
import struct

def name8(s): return s.encode('ascii')[:8].ljust(8, b'\0')

# ---- geometry ---------------------------------------------------------------
# Main room: rectangle, sector 0, floor 0 / ceil 192, tag 1 (target of 3D floor).
# Control box: small separate square, sector 1, floor 64 / ceil 112 (the slab),
#              one linedef carries Sector_Set3DFloor(tag=1, solid, 0, alpha=255).
VERT = [
    (-112, -112), (112, -112), (112, 112), (-112, 112),   # 0-3 main room (tight)
    (240, -48), (368, -48), (368, 48), (240, 48),         # 4-7 control box
]
# SECTORS: (floorh, ceilh, floortex, ceiltex, light, special, tag)
SECT = [
    (0, 128, "FLOOR4_8", "CEIL3_5", 80, 0, 1),    # 0 main room, dim; walls in torch range
    (48, 80, "FLAT14", "FLAT14", 255, 0, 0),      # 1 control -> 3D slab z=48..80 crosses walls
]
# SIDEDEFS: (xoff, yoff, upper, lower, mid, sector)
SIDE = [
    (0, 0, "-", "-", "STARTAN3", 0),  # 0 main-room walls
    (0, 0, "-", "-", "BRICK7", 1),    # 1 control-box walls
]
# LINEDEFS (Hexen): (v1, v2, flags, special, (a0,a1,a2,a3,a4), front, back)
# flags: 0x0001 blocking. Main room walls single-sided -> back = 0xFFFF.
NONE = 0xFFFF
LINE = []
# main room (0-1-2-3-0), all one-sided facing sector 0
for a, b in [(0,1),(1,2),(2,3),(3,0)]:
    LINE.append((a, b, 0x0001, 0, (0,0,0,0,0), 0, NONE))
# control box (4-5-6-7-4); the FIRST edge carries the 3D-floor special
first = True
for a, b in [(4,5),(5,6),(6,7),(7,4)]:
    if first:
        LINE.append((a, b, 0x0001, 160, (1, 1, 0, 255, 0), 1, NONE))  # Sector_Set3DFloor
        first = False
    else:
        LINE.append((a, b, 0x0001, 0, (0,0,0,0,0), 1, NONE))
# ---- things (Hexen): (tid, x, y, z, angle, type, flags, special, args) -------
FL = 0x07 | 0x7e0 & 0  # skill 1-5
FLAGS = 0x0007 | 0x00e0  # all skills + single/coop/dm classes present
THING = [
    (0, -256, 0, 0, 0,  1, 0x0007|0x00e0, 0, (0,0,0,0,0)),   # player 1 start
    (0,    0, 0, 0, 0, 57, 0x0007|0x00e0, 0, (0,0,0,0,0)),   # ShortRedTorch (dynamic light)
]

# ---- lump packers -----------------------------------------------------------
def p_verts():   return b''.join(struct.pack('<hh', x, y) for x,y in VERT)
def p_sects():   return b''.join(struct.pack('<hh8s8shHH', f,c,name8(ft),name8(ct),l,s,t) for f,c,ft,ct,l,s,t in SECT)
def p_sides():   return b''.join(struct.pack('<hh8s8s8sH', xo,yo,name8(u),name8(lo),name8(m),sec) for xo,yo,u,lo,m,sec in SIDE)
def p_lines():
    out=b''
    for v1,v2,fl,sp,ar,fr,bk in LINE:
        out+=struct.pack('<HHHB5BHH', v1,v2,fl,sp,*ar,fr,bk)
    return out
def p_things():
    out=b''
    for tid,x,y,z,ang,ty,fl,sp,ar in THING:
        out+=struct.pack('<HhhhhHHB5B', tid,x,y,z,ang,ty,fl,sp,*ar)
    return out

# map lumps in order; empty node lumps -> engine rebuilds. BEHAVIOR present => Hexen format.
lumps = [
    ("MAP01", b''),
    ("THINGS", p_things()),
    ("LINEDEFS", p_lines()),
    ("SIDEDEFS", p_sides()),
    ("VERTEXES", p_verts()),
    ("SEGS", b''), ("SSECTORS", b''), ("NODES", b''),
    ("SECTORS", p_sects()),
    ("REJECT", b''), ("BLOCKMAP", b''),
    ("BEHAVIOR", b''),
]

# ---- WAD writer -------------------------------------------------------------
data = b''
dirents = []
for nm, payload in lumps:
    dirents.append((12 + len(data), len(payload), nm))
    data += payload
diroff = 12 + len(data)
header = struct.pack('<4sii', b'PWAD', len(lumps), diroff)
directory = b''.join(struct.pack('<ii8s', off, sz, name8(nm)) for off, sz, nm in dirents)
open('/tmp/lighttest.wad','wb').write(header + data + directory)
print(f"wrote /tmp/lighttest.wad: {len(lumps)} lumps, {len(LINE)} lines, {len(SECT)} sectors")

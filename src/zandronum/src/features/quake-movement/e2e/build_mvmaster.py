#!/usr/bin/env python3
# [rc4l] Builds mvmaster.wad, the master E2E fixture for features/quake-movement.
#
# The map is generated rather than hand-drawn so every test position is a stated constant instead
# of a coordinate someone found by walking around. That matters: this feature already produced one
# false positive from an IWAD map, where a pawn wedged in blocked space held full velocity while
# its position never changed -- which reads exactly like a working air wall run.
#
#   python build_mvmaster.py [-o OUT.wad] [--acc PATH_TO_acc.exe]
#
# Emits a PWAD containing MVTEST (UDMF), DECORATE, MAPINFO, LOADACS and the compiled ACS library.

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# --- Map geometry. Every one of these is referenced by a named test in MASTERTEST.md. -----------

ROOM_X0, ROOM_Y0, ROOM_X1, ROOM_Y1 = 0, 0, 5120, 1536
ROOM_FLOOR, ROOM_CEIL = 0, 512

# Edge-jump pit: a 128-unit drop with a hard lip at x=2048, clear of both running lanes.
PIT = (2048, 640, 2560, 1024)
PIT_FLOOR = -128

# Elevator-jump platform: perpetually rising and falling (a DPlat), tag 1. It sits 256 below the
# room so each leg lasts ~64 tics, which is long enough to aim a jump at the rising phase instead
# of racing a one-shot lift.
LIFT = (3072, 640, 3584, 1024)
LIFT_FLOOR = -256
LIFT_TAG = 1

# [rc4l] A ramp, for +EDGEJUMP. That flag preserves upward velocity into a jump, so reaching it at
# all needs a pawn that is `onground` AND already rising -- a state flat geometry cannot produce.
# Plane_Align on the ramp's WEST edge ties its floor to the room (0) there and tilts it up to
# RAMP_FLOOR at the far side, so running east up it is a genuine uphill.
RAMP = (600, 1100, 1400, 1400)
RAMP_FLOOR = 96

WALL_TEX = "STARTAN2"
FLOOR_TEX = "FLOOR4_8"
CEIL_TEX = "CEIL3_5"

# Player starts. 128 apart so nobody telefrags, all at y < 640 so the run lanes never cross the pit.
PLAYER_STARTS = [(128, 128), (128, 256), (128, 384), (128, 512)]


def build_textmap():
    verts = []
    lines = []
    sides = []
    sectors = []

    def vert(x, y):
        if (x, y) not in verts:
            verts.append((x, y))
        return verts.index((x, y))

    def sector(floor, ceil, tag=0):
        sectors.append((floor, ceil, tag))
        return len(sectors) - 1

    def side(sec, mid="", bot="", top=""):
        sides.append((sec, mid, bot, top))
        return len(sides) - 1

    def rect_loop(x0, y0, x1, y1):
        # [rc4l] Wound so the right-hand side of every line -- normal (dy, -dx) -- faces INWARD,
        # which is what makes sidedef 0 the interior side. Getting this backwards yields a map that
        # loads and is inside out.
        a, b, c, d = vert(x0, y0), vert(x0, y1), vert(x1, y1), vert(x1, y0)
        return [(a, b), (b, c), (c, d), (d, a)]

    room = sector(ROOM_FLOOR, ROOM_CEIL)
    for v1, v2 in rect_loop(ROOM_X0, ROOM_Y0, ROOM_X1, ROOM_Y1):
        lines.append((v1, v2, side(room, mid=WALL_TEX), -1, True, 0))

    inners = (
        (PIT, PIT_FLOOR, 0, False),
        (LIFT, LIFT_FLOOR, LIFT_TAG, False),
        (RAMP, RAMP_FLOOR, 0, True),
    )
    for (x0, y0, x1, y1), floor, tag, slope in inners:
        inner = sector(floor, ROOM_CEIL, tag)
        for index, (v1, v2) in enumerate(rect_loop(x0, y0, x1, y1)):
            front = side(inner, bot=WALL_TEX)
            back = side(room, bot=WALL_TEX)
            # rect_loop emits the west edge first, which is the one the ramp is aligned against.
            special = 181 if (slope and index == 0) else 0  # 181 = Plane_Align
            lines.append((v1, v2, front, back, False, special))

    out = ['namespace = "zdoom";', ""]
    for x, y in verts:
        out.append("vertex { x = %d.0; y = %d.0; }" % (x, y))
    out.append("")
    for v1, v2, front, back, solid, special in lines:
        fields = ["v1 = %d;" % v1, "v2 = %d;" % v2, "sidefront = %d;" % front]
        if back >= 0:
            fields.append("sideback = %d;" % back)
            fields.append("twosided = true;")
        if solid:
            fields.append("blocking = true;")
        if special:
            # arg0 = 1 slopes the FRONT sector's floor to meet the back sector at this line.
            fields.append("special = %d; arg0 = 1;" % special)
        out.append("linedef { %s }" % " ".join(fields))
    out.append("")
    for sec, mid, bot, top in sides:
        fields = ["sector = %d;" % sec]
        if mid:
            fields.append('texturemiddle = "%s";' % mid)
        if bot:
            fields.append('texturebottom = "%s";' % bot)
        if top:
            fields.append('texturetop = "%s";' % top)
        out.append("sidedef { %s }" % " ".join(fields))
    out.append("")
    for floor, ceil, tag in sectors:
        fields = [
            "heightfloor = %d;" % floor,
            "heightceiling = %d;" % ceil,
            'texturefloor = "%s";' % FLOOR_TEX,
            'textureceiling = "%s";' % CEIL_TEX,
            "lightlevel = 192;",
        ]
        if tag:
            fields.append("id = %d;" % tag)
        out.append("sector { %s }" % " ".join(fields))
    out.append("")
    for index, (x, y) in enumerate(PLAYER_STARTS):
        out.append(
            "thing { x = %d.0; y = %d.0; angle = 0; type = %d; "
            "skill1 = true; skill2 = true; skill3 = true; skill4 = true; skill5 = true; "
            "single = true; coop = true; dm = true; }" % (x, y, index + 1)
        )
    return ("\n".join(out) + "\n").encode("ascii")


def compile_acs(acc_exe):
    src = os.path.join(HERE, "mvmaster.acs")
    tmp = tempfile.mkdtemp(prefix="mvacs")
    try:
        obj = os.path.join(tmp, "MVMSTR.o")
        # [rc4l] acc resolves #include against its own directory, so run it from there.
        result = subprocess.run(
            [acc_exe, "-i", os.path.dirname(acc_exe), src, obj],
            capture_output=True, text=True,
        )
        if result.returncode != 0 or not os.path.exists(obj):
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit("acc failed (exit %d)" % result.returncode)
        with open(obj, "rb") as handle:
            return handle.read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def write_wad(path, lumps):
    data = bytearray()
    directory = []
    offset = 12
    for name, payload in lumps:
        directory.append((offset, len(payload), name))
        data += payload
        offset += len(payload)
    with open(path, "wb") as handle:
        handle.write(b"PWAD")
        handle.write(struct.pack("<ii", len(lumps), 12 + len(data)))
        handle.write(data)
        for filepos, size, name in directory:
            handle.write(struct.pack("<ii", filepos, size))
            handle.write(name.upper().encode("ascii").ljust(8, b"\0")[:8])


def read(name):
    with open(os.path.join(HERE, name), "rb") as handle:
        return handle.read()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--out", default=os.path.join(HERE, "mvmaster.wad"))
    parser.add_argument("--acc", default=r"C:\Users\-MGO-\Desktop\FUA\acc-1.60-win32\acc.exe")
    # [rc4l] The control wad is the same map with NO DECORATE, MAPINFO player classes or ACS, so it
    # loads on an engine that predates this feature. That is what makes "MvType 0 is unchanged" a
    # measurement across two builds rather than an assertion about one.
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()

    if args.control:
        write_wad(args.out, [
            ("MVTEST", b""),
            ("TEXTMAP", build_textmap()),
            ("ENDMAP", b""),
            ("MAPINFO", b'map MVTEST "Quake Movement Control"\n{\n\tCluster = 1\n\tMusic = ""\n'
                        b'\tNext = "MVTEST"\n\tSecretNext = "MVTEST"\n\tNoIntermission\n'
                        b'\tAllowRespawn\n\tAllowCrouch\n\tAllowJump\n}\n\nclusterdef 1\n{\n\thub\n}\n'),
        ])
        print("wrote %s (control, %d bytes)" % (args.out, os.path.getsize(args.out)))
        return

    lumps = [
        ("MVTEST", b""),
        ("TEXTMAP", build_textmap()),
        ("ENDMAP", b""),
        ("DECORATE", read("mvmaster_decorate.txt")),
        ("MAPINFO", read("mvmaster_mapinfo.txt")),
        ("LOADACS", b"MVMSTR\n"),
        # [rc4l] A_START/A_END is what puts the object lump in ns_acslibrary; LOADACS looks it up
        # in that namespace only, so an unmarked lump is silently never loaded.
        ("A_START", b""),
        ("MVMSTR", compile_acs(args.acc)),
        ("A_END", b""),
    ]
    write_wad(args.out, lumps)
    print("wrote %s (%d bytes, %d lumps)" % (args.out, os.path.getsize(args.out), len(lumps)))


if __name__ == "__main__":
    main()

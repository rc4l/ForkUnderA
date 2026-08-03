#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] wire-enum-snapshot.py -- pin the VALUES of enums that travel on the wire.
#
# protocol-snapshot.py pins the shape of every command: its id, and each field's type,
# order and condition. What it cannot see is the value space of an enum carried INSIDE a
# field. SERVERCOMMANDS_DoCeiling( DCeiling::ECeiling Type, ... ) writes Type as an
# integer; inserting, removing or reordering an enumerator in ECeiling changes what that
# integer means while the command's signature stays byte-identical. The command count
# still reads 192 and nothing complains.
#
# Six sector movers do exactly this today (DFloor, DCeiling, DPlat, DElevator, DPillar),
# and the same enums are written into savegames (p_ceiling.cpp: `arc << m_Type`), so a
# renumber breaks saved games as well as multiplayer.
#
# This is upstream-invisible. ZDoom is peer-to-peer lockstep: no SERVERCOMMANDS, no enum
# on the wire, so an upstream commit is free to renumber one and did -- uzdoom@df0d3543a
# removes DCeiling::ceilCrushAndRaiseDist without touching SAVEVER, which is correct for
# them and load-bearing for us. Porting upstream faithfully therefore REQUIRES a checker
# on our side; that is the whole point of this file.
#
#   tools/wire-enum-snapshot.py --write   # accept a deliberate change (bump the versions!)
#   tools/wire-enum-snapshot.py --check   # CI: fail if any wire-carried enum renumbered
#
# WHICH enums are wire-carried is DERIVED, not listed: any `Class::EnumType` appearing as
# a parameter of a SERVERCOMMANDS_* declaration qualifies. Add a command taking a new enum
# and it is covered automatically, with no list to keep in sync.

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src/zandronum/src")
COMMANDS_H = os.path.join(SRC, "sv_commands.h")
GOLDEN = os.path.join(ROOT, "tools/data/wire-enum-snapshot.txt")

HEADER = (
    "# Values of enums carried inside network commands. DERIVED -- do not hand-edit.\n"
    "# Regenerate with: tools/wire-enum-snapshot.py --write\n"
    "# A change here alters what an existing command's bytes MEAN. Bump SAVEVER (these\n"
    "# enums are serialized) and DEMOGAMEVERSION (demos replay the commands) in the same\n"
    "# commit.\n"
)


def wire_enum_types():
    """Class::EnumType names used as parameters of any SERVERCOMMANDS_* declaration."""
    with open(COMMANDS_H, encoding="utf-8", errors="ignore") as fh:
        text = fh.read()
    found = set()
    for decl in re.findall(r"\bSERVERCOMMANDS_\w+\s*\(([^;]*?)\)\s*;", text, re.S):
        for cls, enum in re.findall(r"\b([A-Z]\w+)::([A-Z]\w+)\b", decl):
            found.add((cls, enum))
    return sorted(found)


def enum_values(cls, enum):
    """Enumerator -> value for `enum <enum>` declared inside `class/struct <cls>`.

    C rules: an enumerator with no initialiser is previous + 1, starting at 0. That is
    exactly why an insertion in the middle is dangerous and why this file exists.
    """
    for dirpath, _, names in os.walk(SRC):
        for name in names:
            if not name.endswith((".h", ".hpp")):
                continue
            path = os.path.join(dirpath, name)
            try:
                text = open(path, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            cm = re.search(r"\b(?:class|struct)\s+%s\b" % re.escape(cls), text)
            if not cm:
                continue
            em = re.search(r"\benum\s+%s\s*\{(.*?)\}" % re.escape(enum), text[cm.start():], re.S)
            if not em:
                continue
            out, nxt = [], 0
            for raw in em.group(1).split(","):
                raw = re.sub(r"//.*", "", raw)
                raw = re.sub(r"/\*.*?\*/", "", raw, flags=re.S).strip()
                if not raw:
                    continue
                if "=" in raw:
                    key, _, val = raw.partition("=")
                    val = val.strip()
                    try:
                        nxt = int(val, 0)
                    except ValueError:
                        nxt = None          # computed; record the expression verbatim
                    out.append((key.strip(), nxt if nxt is not None else val))
                    if isinstance(nxt, int):
                        nxt += 1
                else:
                    out.append((raw, nxt))
                    nxt = nxt + 1 if isinstance(nxt, int) else nxt
            return out, os.path.relpath(path, ROOT)
    return None, None


def signature():
    lines = []
    for cls, enum in wire_enum_types():
        values, where = enum_values(cls, enum)
        if values is None:
            lines.append("%s::%s  !! NOT FOUND -- check the declaration" % (cls, enum))
            continue
        lines.append("%s::%s  (%s)" % (cls, enum, where))
        for key, val in values:
            lines.append("    %-40s %s" % (key, val))
    return "\n".join(lines) + "\n"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sig = signature()

    if mode == "--write":
        os.makedirs(os.path.dirname(GOLDEN), exist_ok=True)
        with open(GOLDEN, "w") as fh:
            fh.write(HEADER + sig)
        n = sum(1 for l in sig.splitlines() if not l.startswith("    "))
        print("wire-enum-snapshot: wrote golden (%d wire-carried enums)." % n)
        return 0

    if not os.path.exists(GOLDEN):
        print("wire-enum-snapshot: no golden at %s -- run --write once" % GOLDEN,
              file=sys.stderr)
        return 1
    with open(GOLDEN) as fh:
        golden = fh.read()
    if golden == HEADER + sig:
        n = sum(1 for l in sig.splitlines() if not l.startswith("    "))
        print("wire-enum-snapshot: wire-carried enum values unchanged (%d enums)." % n)
        return 0

    import difflib
    print("wire-enum-snapshot: A WIRE-CARRIED ENUM CHANGED VALUE.\n", file=sys.stderr)
    for line in difflib.unified_diff(golden.splitlines(), (HEADER + sig).splitlines(),
                                     "golden", "current", lineterm="", n=2):
        print(line, file=sys.stderr)
    print("\nThese values ride inside existing commands, so the command signature is\n"
          "unchanged and protocol-snapshot will NOT catch this. A client reading the old\n"
          "numbering acts on the wrong type, and savegames storing it break the same way.\n"
          "If deliberate: bump SAVEVER and DEMOGAMEVERSION, then\n"
          "`tools/wire-enum-snapshot.py --write` in the SAME commit.\n", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

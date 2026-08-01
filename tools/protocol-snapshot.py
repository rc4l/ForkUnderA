#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# protocol-snapshot.py -- pin the client/server WIRE FORMAT so a backport can't
# silently change it.
#
# Every SERVERCOMMANDS_* command is generated from src/zandronum/protocolspec/*.txt.
# A change there -- a field added, reordered, retyped, made (un)conditional, or a
# command inserted (which shifts every later command's wire ID) -- changes what
# goes on the network. Offline builds and single-player never notice; multiplayer
# silently desyncs. This tool derives a canonical signature of the whole protocol
# by REUSING THE GENERATOR'S OWN PARSER (so the snapshot cannot drift from the code
# the engine actually runs) and diffs it against a committed golden.
#
#   tools/protocol-snapshot.py --write   # regenerate the golden (deliberate change)
#   tools/protocol-snapshot.py --check    # CI: fail if the wire format moved
#
# The signature is TYPE-based, not name-based: renaming a field is not a wire
# change and must not trip the check; changing its type, order, condition, or the
# command's ID is. Each line is:
#
#   <id> <SVC|SVC2>_<NAME> [flags] : <type>[<spec>][?] <type>[<spec>][?] ...
#
# where a trailing '?' marks a conditional field and [flags] notes Unreliable /
# Extended framing -- both wire-visible.
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "src/zandronum/protocolspec/spec.txt")
GOLDEN = os.path.join(ROOT, "tools/data/protocol-snapshot.txt")
GEN = os.path.join(ROOT, "src/zandronum/protocolspec/generator")

HEADER = (
    "# AUTO-GENERATED wire-format snapshot -- do not hand-edit.\n"
    "# Regenerate with: tools/protocol-snapshot.py --write\n"
    "# One line per network command: <id> <enum> [flags] : <field-types-in-order>.\n"
    "# A diff here means a backport changed the client/server wire format. Confirm it\n"
    "# is intentional AND that older clients can still parse it before updating this file.\n"
)


def load_spec():
    sys.path.insert(0, GEN)
    from spec import NetworkSpec  # the generator's own parser -- single source of truth
    spec = NetworkSpec()
    spec.loadfromfile(SPEC)
    return spec


def field_sig(param):
    """Wire signature of one field: type, specialization, and the condition that
    gates it. No field NAME -- a rename is not a wire change and must not fire."""
    sig = param.typename
    if getattr(param, "specialization", None):
        sig += "<%s>" % param.specialization
    cond = getattr(param, "condition", None)
    if cond:
        # Which flag/bit gates this optional field -- changing it changes the wire.
        sig += "?{%s}" % " ".join(str(cond).split())
    return sig


def snapshot():
    spec = load_spec()
    lines = []
    # Commands live under each protocol; iterate protocols in definition order, and
    # within each, number base (SVC) and extended (SVC2) commands separately by parse
    # order -- exactly how the generated SVC/SVC2 enums assign wire IDs.
    for proto_name, proto in spec.protocols.items():
        lines.append("## protocol %s" % proto_name)
        counters = {False: 0, True: 0}
        for cmd in proto["commands"].values():
            cid = counters[cmd.extended]
            counters[cmd.extended] += 1
            flags = []
            if cmd.extended:
                flags.append("Extended")
            if getattr(cmd, "unreliable", False):
                flags.append("Unreliable")
            flagstr = (" [%s]" % ",".join(flags)) if flags else ""
            fields = " ".join(field_sig(p) for p in cmd) or "(no fields)"
            lines.append("%3d %-38s%s : %s" % (cid, cmd.enumname, flagstr, fields))
    return HEADER + "\n".join(lines) + "\n"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--check"
    current = snapshot()
    if mode == "--write":
        os.makedirs(os.path.dirname(GOLDEN), exist_ok=True)
        with open(GOLDEN, "w") as f:
            f.write(current)
        print("protocol-snapshot: wrote golden (%d commands)" % (current.count("\n") - 6))
        return 0
    # --check
    if not os.path.exists(GOLDEN):
        print("protocol-snapshot: no golden at %s -- run --write once" % GOLDEN, file=sys.stderr)
        return 1
    with open(GOLDEN) as f:
        golden = f.read()
    if current == golden:
        print("protocol-snapshot: wire format unchanged (%d commands)." % (current.count("\n") - 6))
        return 0
    import difflib
    print("protocol-snapshot: THE WIRE FORMAT CHANGED. A backport altered the client/server "
          "protocol.\nConfirm it is intentional and older clients still parse it, then run "
          "`tools/protocol-snapshot.py --write` to accept it.\n", file=sys.stderr)
    for line in difflib.unified_diff(golden.splitlines(True), current.splitlines(True),
                                     "golden", "current"):
        sys.stderr.write(line)
    return 1


if __name__ == "__main__":
    sys.exit(main())

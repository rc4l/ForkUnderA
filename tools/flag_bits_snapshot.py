#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# [Doruk-Aksoy] flag_bits_snapshot.py -- pin the bit of every flag DECORATE can set, since saves store flag words raw.
#
# A savegame writes each flag word as an integer, so moving a flag to another bit, as taking upstream's numbering does, makes every
# older save load with the wrong flags while nothing else notices: DECORATE and ACS name flags, and both ends of a netgame share a build.
#
#   tools/flag_bits_snapshot.py --check   # CI: fail when any flag's bit differs from the golden
#   tools/flag_bits_snapshot.py --write   # accept it; a moved or removed bit needs SAVEVER past the golden's and old saves remapped
#
# WHICH flags is DERIVED: every DEFINE_FLAG/DEFINE_FLAG2 row of thingdef_data.cpp, each with the field it lives in.

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src/zandronum/src")
TABLE = os.path.join(SRC, "thingdef/thingdef_data.cpp")
VERSION_H = os.path.join(SRC, "version.h")
GOLDEN = os.path.join(ROOT, "tools/data/flag-bits-snapshot.txt")

HEADER = (
    "# The bit of every flag DECORATE can set, by the field that holds it. DERIVED -- do not hand-edit.\n"
    "# Regenerate with: tools/flag_bits_snapshot.py --write\n"
)


def flag_rows(table_text):
    """(class, field, symbol, name) for each DEFINE_FLAG and DEFINE_FLAG2 row of the flag table."""
    rows = []
    table_text = "\n".join(l for l in table_text.splitlines() if not l.lstrip().startswith("#"))
    for prefix, name, cls, field in re.findall(r"\bDEFINE_FLAG\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", table_text):
        rows.append((cls, field, "%s_%s" % (prefix, name), name))
    for symbol, name, cls, field in re.findall(r"\bDEFINE_FLAG2\(\s*([^,]+?)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", table_text):
        rows.append((cls, field, symbol.strip(), name))
    return rows


def enumerators(text):
    """name -> value expression for every `NAME = expr` inside an enum body of `text`."""
    out = {}
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    for key, val in re.findall(r"^[ \t]*#define[ \t]+(\w+)[ \t]+([^\n]+)$", text, re.M):
        out.setdefault(key, val.strip())
    for body in re.findall(r"\benum\b[^{;]*\{(.*?)\}", text, re.S):
        for raw in body.split(","):
            raw = raw.strip()
            if "=" in raw:
                key, _, val = raw.partition("=")
                out.setdefault(key.strip(), val.strip())
    return out


SAFE = re.compile(r"^[0-9a-fA-FxXuUlL\s|&<>()+~-]*$")


def evaluate(expr, names, depth=0):
    """The integer value of an enumerator expression made of numbers, other enumerators, | & << >> + - ~, or None."""
    if depth > 8:
        return None
    def sub(m):
        word = m.group(0)
        if re.match(r"^(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$", word):
            return str(int(re.sub(r"[uUlL]+$", "", word), 0))
        if word in names:
            v = evaluate(names[word], names, depth + 1)
            if v is not None:
                return str(v)
        return "?"
    flat = re.sub(r"\(unsigned int\)|\(DWORD\)|\(int\)", "", expr)
    flat = re.sub(r"\b\w+\b", sub, flat)
    if "?" in flat or not SAFE.match(flat):
        return None
    try:
        return int(eval(flat, {"__builtins__": {}}, {})) & 0xFFFFFFFF
    except Exception:
        return None


def header_names():
    """Enumerators from every header under src, actor.h's first since that is where actor flags live."""
    paths = []
    for dirpath, _, files in os.walk(SRC):
        for f in files:
            if f.endswith(".h"):
                paths.append(os.path.join(dirpath, f))
    paths.sort(key=lambda p: (os.path.basename(p) != "actor.h", p))
    names = {}
    for p in paths:
        try:
            text = open(p, encoding="latin-1").read()
        except OSError:
            continue
        for k, v in enumerators(text).items():
            names.setdefault(k, v)
    return names


def signature(table_text, names):
    lines = []
    for cls, field, symbol, name in sorted(set(flag_rows(table_text))):
        value = evaluate(symbol, names) if symbol not in names else evaluate(names[symbol], names)
        lines.append("%s.%s %s %s" % (cls, field, name, "0x%08x" % value if value is not None else "?" + symbol))
    return lines


def savever(text):
    m = re.search(r"^#define SAVEVER\s+(\d+)", text, re.M)
    return int(m.group(1)) if m else None


def moved(golden_lines, current_lines):
    """Golden lines whose flag now sits at another bit or is gone, which an older save would misread."""
    now = {l.rsplit(" ", 1)[0]: l for l in current_lines}
    return [l for l in golden_lines if now.get(l.rsplit(" ", 1)[0]) != l]


def parse_golden(text):
    lines = [l for l in text.splitlines() if l and not l.startswith("#")]
    ver = None
    if lines and lines[0].startswith("savever "):
        ver = int(lines[0].split()[1])
        lines = lines[1:]
    return ver, lines


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--check"
    current = signature(open(TABLE, encoding="latin-1").read(), header_names())
    ver = savever(open(VERSION_H, encoding="latin-1").read())
    unresolved = [l for l in current if " ?" in l]
    if unresolved:
        print("flag-bits-snapshot: could not work out these flags' bits:\n  " + "\n  ".join(unresolved), file=sys.stderr)
        return 1
    golden_ver, golden = (None, [])
    if os.path.exists(GOLDEN):
        golden_ver, golden = parse_golden(open(GOLDEN, encoding="utf-8").read())

    if mode == "--write":
        gone = moved(golden, current)
        if gone and golden_ver is not None and ver <= golden_ver:
            print("flag-bits-snapshot: refusing to write: these flags moved or went while SAVEVER stayed %d:\n  %s\n"
                  "Bump SAVEVER (version.h) and move older saves' bits in AActor::Serialize (features/decorate-uzdoom/computation/flagrehome_compute.h), then --write."
                  % (ver, "\n  ".join(gone)), file=sys.stderr)
            return 1
        os.makedirs(os.path.dirname(GOLDEN), exist_ok=True)
        with open(GOLDEN, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(HEADER + "savever %d\n" % ver + "\n".join(current) + "\n")
        print("flag-bits-snapshot: wrote golden (%d flags, SAVEVER %d)." % (len(current), ver))
        return 0

    if not golden:
        print("flag-bits-snapshot: no golden at %s -- run --write once" % GOLDEN, file=sys.stderr)
        return 1
    if golden == current:
        print("flag-bits-snapshot: flag bits unchanged (%d flags)." % len(current))
        return 0
    gone = moved(golden, current)
    added = sorted(set(current) - set(golden))
    if gone:
        print("flag-bits-snapshot: FLAGS MOVED OR WENT, so older saves would load them as other flags:\n  " + "\n  ".join(gone), file=sys.stderr)
        print("Bump SAVEVER, move older saves' bits in AActor::Serialize, then --write in the same commit.", file=sys.stderr)
    if added:
        print("flag-bits-snapshot: new flags (safe for saves; accept with --write):\n  " + "\n  ".join(added), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

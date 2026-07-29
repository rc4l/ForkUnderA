# [rc4l] Pure crash-address rebasing shared by the cloud pipeline (crash_sync.py) and the local
# symbolizer (symbolicate_local.py). No I/O, no env, no network -- just the integer math that turns
# the runtime instruction addresses in a crash event into the *static* addresses a symbol file
# (dSYM / .debug / .pdb) is linked at. Keeping it here means both tools rebase IDENTICALLY, and the
# math is unit-tested off any pipeline (tools/crash_rebase_test.py).
#
# Why rebasing is needed: at crash time the engine stamps `zx_image_base` = the main module's
# runtime load address, and each frame carries its runtime `instruction_addr`. The symbol file,
# though, is linked at a fixed preferred base (0x1_0000_0000 on macOS, 0 on Linux, 0x1_4000_0000 on
# Windows). static_addr = preferred_base + (runtime_addr - image_base). We also drop frames outside
# the main module (system libraries) -- we only ship symbols for our own binary.
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# platform -> (preferred link base, symbol-file path inside the release symbols zip). The symbol
# sub-path is unused by the pure math but lives here so both tools agree on one platform table.
PLAT = {
    "macos":   (0x100000000, "zandronum.dSYM/Contents/Resources/DWARF/zandronum"),
    "linux":   (0,           "zandronum.debug"),
    "windows": (0x140000000, None),        # first *.pdb found in the zip
}

# A frame's runtime address counts as "in the main module" if it sits within this span above the
# image base. 256 MiB comfortably covers the engine's text/data while excluding system libraries
# that load at unrelated addresses.
MAIN_MODULE_SPAN = 256 << 20


def to_addr(s):
    """Parse a hex address ('0x1a2b', '1A2B', '  0x1a2b ') to int; None if empty/unparseable.

    Returns None (rather than raising) so callers can skip junk frames without special-casing."""
    if s is None:
        return None
    t = str(s).strip().lower()
    if t.startswith("0x"):
        t = t[2:]
    if not t:
        return None
    try:
        return int(t, 16)
    except ValueError:
        return None


def rebase_main_module(runtime_addrs, image_base, preferred_base, span=MAIN_MODULE_SPAN):
    """Rebase runtime instruction addresses to static (symbol-file) addresses.

    runtime_addrs: iterable of ints (or None, which is skipped) in frame order.
    Keeps only frames whose offset from image_base is in [0, span) -- i.e. the main module.
    Returns a list of (original_index, static_addr), preserving order. This is exactly the mapping
    the cloud pipeline feeds to llvm-symbolizer; the local tool feeds the same static addrs to atos
    with `-l preferred_base` (slide 0), so both resolve to the identical source lines."""
    out = []
    for i, ia in enumerate(runtime_addrs):
        if ia is None:
            continue
        off = ia - image_base
        if 0 <= off < span:
            out.append((i, preferred_base + off))
    return out

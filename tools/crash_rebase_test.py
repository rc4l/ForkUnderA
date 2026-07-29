# [rc4l] Tests for the pure crash-address rebasing (tools/crash_rebase.py). Run: python3 -m unittest
# tools.crash_rebase_test  (or `python3 tools/crash_rebase_test.py`). No engine, no network.
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from crash_rebase import to_addr, rebase_main_module, MAIN_MODULE_SPAN, PLAT


class ToAddr(unittest.TestCase):
    def test_parses_with_and_without_0x(self):
        self.assertEqual(to_addr("0x1a2b"), 0x1A2B)
        self.assertEqual(to_addr("1a2b"), 0x1A2B)

    def test_case_and_surrounding_whitespace(self):
        self.assertEqual(to_addr("  0X00Ff  "), 0xFF)

    def test_empty_and_junk_return_none(self):
        for bad in (None, "", "   ", "0x", "nope", "0xzz"):
            self.assertIsNone(to_addr(bad), bad)

    def test_is_hex_only_by_contract(self):
        # GlitchTip's instruction_addr is always a hex string ("0x..."); to_addr is hex-only, so a
        # bare decimal is (deliberately) read as hex -- "10" is 0x10, not ten.
        self.assertEqual(to_addr("10"), 0x10)
        self.assertEqual(to_addr("0x140001000"), 0x140001000)


class RebaseMainModule(unittest.TestCase):
    def test_macos_rebases_offset_onto_preferred_base(self):
        pref, _ = PLAT["macos"]
        base = 0x102000000                       # runtime load address (ASLR slide)
        # crash site at base+0x1234, one caller at base+0x1000.
        addrs = [base + 0x1000, base + 0x1234]
        out = rebase_main_module(addrs, base, pref)
        self.assertEqual(out, [(0, pref + 0x1000), (1, pref + 0x1234)])

    def test_drops_system_frames_outside_the_module(self):
        pref, _ = PLAT["macos"]
        base = 0x102000000
        addrs = [
            base + 0x50,                         # in module -> kept
            0x7FFF20000000,                      # system dylib, far away -> dropped
            base - 0x10,                         # below base (negative offset) -> dropped
            base + 0x40,                         # in module -> kept
        ]
        out = rebase_main_module(addrs, base, pref)
        self.assertEqual(out, [(0, pref + 0x50), (3, pref + 0x40)])   # indices are ORIGINAL positions

    def test_span_boundary_is_half_open(self):
        pref, _ = PLAT["macos"]
        base = 0x100000000
        addrs = [base + MAIN_MODULE_SPAN - 1,    # last in-range offset -> kept
                 base + MAIN_MODULE_SPAN]         # exactly span -> dropped (half-open)
        out = rebase_main_module(addrs, base, pref)
        self.assertEqual(out, [(0, pref + MAIN_MODULE_SPAN - 1)])

    def test_none_frames_are_skipped_without_shifting_indices(self):
        pref, _ = PLAT["macos"]
        base = 0x100000000
        addrs = [base + 0x10, None, base + 0x20]
        out = rebase_main_module(addrs, base, pref)
        self.assertEqual(out, [(0, pref + 0x10), (2, pref + 0x20)])

    def test_linux_preferred_base_zero_is_identity_on_offset(self):
        pref, _ = PLAT["linux"]                  # 0 -> static addr == offset
        base = 0x555555554000
        addrs = [base + 0x1140]
        out = rebase_main_module(addrs, base, pref)
        self.assertEqual(out, [(0, 0x1140)])

    def test_empty_input(self):
        self.assertEqual(rebase_main_module([], 0x100000000, 0x100000000), [])


if __name__ == "__main__":
    unittest.main()

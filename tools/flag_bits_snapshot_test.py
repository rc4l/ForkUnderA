#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# [Doruk-Aksoy] Tests for the flag-bit snapshot on made-up trees, stdlib only: python3 tools/flag_bits_snapshot_test.py
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flag_bits_snapshot as snap  # noqa: E402

TABLE = ('#define DEFINE_FLAG(prefix, name, type, variable) { prefix##_##name }\n'
         'DEFINE_FLAG(MF8, E1M8BOSS, AActor, flags8),\n'
         'DEFINE_FLAG2(MF2_RIP, RIPPER, AActor, flags2),\n'
         'DEFINE_FLAG2(BOUNCE_Doom, DOOMBOUNCE, AActor, BounceFlags),\n'
         'DEFINE_FLAG(FX, ROCKET, AActor, effects),\n')
HEADER = ('enum ActorFlag8 { MF8_FRIGHTENING = 0x1, // a comment, with a comma\n MF8_E1M8BOSS = 0x00020000, };\n'
          'enum { MF2_RIP = 1 << 3, };\n'
          'enum EBounce { BOUNCE_Walls = 1<<0, BOUNCE_Floors = 1<<1, BOUNCE_Doom = BOUNCE_Walls|BOUNCE_Floors, };\n'
          '#define FX_ROCKET 0x00000001\n')


class FlagBitsSnapshot(unittest.TestCase):
    def test_rows_skip_the_macro_and_read_both_forms(self):
        rows = snap.flag_rows(TABLE)
        self.assertIn(('AActor', 'flags8', 'MF8_E1M8BOSS', 'E1M8BOSS'), rows)
        self.assertIn(('AActor', 'flags2', 'MF2_RIP', 'RIPPER'), rows)
        self.assertFalse(any(r[0] == 'type' for r in rows))

    def test_values_come_from_enums_defines_and_expressions(self):
        lines = snap.signature(TABLE, snap.enumerators(HEADER))
        self.assertIn('AActor.flags8 E1M8BOSS 0x00020000', lines)
        self.assertIn('AActor.flags2 RIPPER 0x00000008', lines)
        self.assertIn('AActor.BounceFlags DOOMBOUNCE 0x00000003', lines)
        self.assertIn('AActor.effects ROCKET 0x00000001', lines)

    def test_an_unknown_name_or_a_call_is_unresolved(self):
        self.assertIsNone(snap.evaluate('MISSING | 1', {}))
        self.assertIsNone(snap.evaluate('open(1)', {}))
        self.assertIsNone(snap.evaluate('A', {'A': 'A'}))
        self.assertEqual(snap.evaluate('0x10u', {}), 16)

    def test_a_flag_at_another_bit_or_gone_is_moved_and_a_new_one_is_not(self):
        golden = ['AActor.flags8 E1M8BOSS 0x00000004', 'AActor.flags9 NORIPSOUND 0x00000001', 'AActor.flags2 RIPPER 0x00000008']
        current = ['AActor.flags8 E1M8BOSS 0x00020000', 'AActor.flags2 RIPPER 0x00000008', 'AActor.fua_flags NORIPSOUND 0x00000001']
        self.assertEqual(snap.moved(golden, current), golden[:2])
        self.assertEqual(snap.moved(golden[2:], current), [])

    def test_the_golden_names_its_save_version(self):
        self.assertEqual(snap.parse_golden('# c\nsavever 4538\nA.b C 0x1\n'), (4538, ['A.b C 0x1']))
        self.assertEqual(snap.savever('#define SAVEVER 4538\n'), 4538)


class FlagBitsMain(unittest.TestCase):
    def run_main(self, root, mode):
        saved = (snap.TABLE, snap.VERSION_H, snap.GOLDEN, snap.SRC)
        snap.SRC, snap.TABLE = root, os.path.join(root, 'table.cpp')
        snap.VERSION_H, snap.GOLDEN = os.path.join(root, 'version.h'), os.path.join(root, 'golden.txt')
        out, err = io.StringIO(), io.StringIO()
        try:
            with redirect_stdout(out), redirect_stderr(err):
                code = snap.main(['x', mode])
        finally:
            snap.TABLE, snap.VERSION_H, snap.GOLDEN, snap.SRC = saved
        return code, out.getvalue() + err.getvalue()

    def tree(self, root, bit, savever):
        open(os.path.join(root, 'table.cpp'), 'w').write('DEFINE_FLAG(MF8, E1M8BOSS, AActor, flags8),\n')
        open(os.path.join(root, 'actor.h'), 'w').write('enum { MF8_E1M8BOSS = %s, };\n' % bit)
        open(os.path.join(root, 'version.h'), 'w').write('#define SAVEVER %d\n' % savever)

    def test_a_moved_bit_fails_the_check_and_needs_a_save_version_bump_to_write(self):
        with tempfile.TemporaryDirectory() as root:
            self.tree(root, '0x4', 4532)
            self.assertEqual(self.run_main(root, '--write')[0], 0)
            self.assertEqual(self.run_main(root, '--check')[0], 0)
            self.tree(root, '0x00020000', 4532)
            code, text = self.run_main(root, '--check')
            self.assertEqual(code, 1)
            self.assertIn('FLAGS MOVED OR WENT', text)
            code, text = self.run_main(root, '--write')
            self.assertEqual(code, 1)
            self.assertIn('refusing to write', text)
            self.tree(root, '0x00020000', 4538)
            self.assertEqual(self.run_main(root, '--write')[0], 0)
            self.assertEqual(self.run_main(root, '--check')[0], 0)

    def test_a_new_flag_fails_the_check_until_written_and_no_golden_fails(self):
        with tempfile.TemporaryDirectory() as root:
            self.tree(root, '0x4', 4532)
            self.assertIn('no golden', self.run_main(root, '--check')[1])
            self.run_main(root, '--write')
            open(os.path.join(root, 'table.cpp'), 'a').write('DEFINE_FLAG(MF8, E2M8BOSS, AActor, flags8),\n')
            open(os.path.join(root, 'actor.h'), 'a').write('enum { MF8_E2M8BOSS = 0x8, };\n')
            code, text = self.run_main(root, '--check')
            self.assertEqual(code, 1)
            self.assertIn('new flags', text)
            self.assertEqual(self.run_main(root, '--write')[0], 0)

    def test_an_unresolved_flag_fails(self):
        with tempfile.TemporaryDirectory() as root:
            self.tree(root, 'SOMETHING_ELSE', 4532)
            code, text = self.run_main(root, '--check')
            self.assertEqual(code, 1)
            self.assertIn('could not work out', text)


if __name__ == '__main__':
    unittest.main()

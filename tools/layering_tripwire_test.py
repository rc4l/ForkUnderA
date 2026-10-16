#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Tests for the layering tripwire on made-up trees, stdlib only: python3 tools/layering_tripwire_test.py
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layering_tripwire import check  # noqa: E402


def tree(**files):
    """[rc4l] A source tree as {src-relative path: text}, with '__' for '/' and '_dot_' for '.' in the names."""
    return {k.replace('__', '/').replace('_dot_', '.'): v for k, v in files.items()}


INC = '#include "%s"\n'


class LayeringTripwire(unittest.TestCase):
    def test_one_way_includes_are_clean(self):
        files = tree(features__a__a_dot_cpp=INC % 'features/b/b.h', features__b__b_dot_h='', features__b__b_dot_cpp=INC % 'features/b/b.h')
        self.assertEqual(check(files), [])

    def test_two_features_including_each_other_are_a_cycle(self):
        files = tree(features__a__a_dot_cpp=INC % 'features/b/b.h', features__a__a_dot_h='', features__b__b_dot_h='', features__b__b_dot_cpp=INC % 'features/a/a.h')
        problems = check(files)
        self.assertEqual(problems[0], 'features include each other in a cycle: a, b')
        self.assertIn('  a -> b: features/a/a.cpp -> features/b/b.h', problems)
        self.assertIn('  b -> a: features/b/b.cpp -> features/a/a.h', problems)

    def test_a_longer_cycle_is_found_whole(self):
        files = tree(features__a__a_dot_cpp=INC % 'features/b/b.h', features__b__b_dot_h=INC % 'features/c/c.h', features__c__c_dot_h=INC % 'features/a/a.h', features__a__a_dot_h='')
        self.assertEqual(check(files)[0], 'features include each other in a cycle: a, b, c')

    def test_pure_computation_headers_close_no_cycle(self):
        files = tree(features__a__a_dot_cpp=INC % 'features/b/computation/b_compute.h', features__a__a_dot_h='', features__b__computation__b_compute_dot_h='', features__b__b_dot_cpp=INC % 'features/a/a.h')
        self.assertEqual(check(files), [])

    def test_a_header_below_another_features_root_is_deep(self):
        files = tree(d_main_dot_cpp=INC % 'features/b/inner/live.h', features__b__inner__live_dot_h='', features__b__b_dot_cpp=INC % 'inner/live.h')
        self.assertEqual(check(files), ["d_main.cpp includes features/b/inner/live.h, below features/b's root"])

    def test_tests_and_missing_headers_are_ignored(self):
        files = tree(features__a__a_test_dot_cpp=INC % 'features/b/inner/live.h', features__b__inner__live_dot_h='', features__a__a_dot_cpp=INC % 'vendor.h')
        self.assertEqual(check(files), [])


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Tests for the boundary tripwire on made-up trees, stdlib only: python3 tools/boundary_tripwire_test.py
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boundary_tripwire import (DOC_BEGIN, DOC_END, check, code_only, counts, doc, exempted, field_patterns, in_set,  # noqa: E402
                               lowered, seams, stale_docs, with_doc)

NL = '\n'

RENDER = {'name': 'render', 'files': ['features/sprites/'], 'exclude': ['*/computation/*', '*_test.cpp'],
          'patterns': [{'name': 'actor', 'regex': r'\bAActor\b'}]}
TABLE = ('ZX_COPY_FIELD(sector, floorplane, secplane_t)\nZX_COPY_FIELD(side, textures, part)\nZX_COPY_FIELD(actor, angle, angle_t)\n'
         '#define ZX_ACTOR_FIELD(member)\nZX_ACTOR_FIELD(sprite)\nZX_ACTOR_DERIVED(const void *, who)   // [rc4l] note\n'
         'ZX_PLAYER_FIELD(viewz)\nZX_PLAYER_DERIVED(std::vector<pspdef_t>, layers)\nZX_LEVEL_FIELD(time)\nZX_LEVEL_DERIVED(FTextureID, sky1)\n'
         'ZX_DECAL_FIELD(Alpha)\n')


class BoundaryTripwire(unittest.TestCase):
    def test_console_commands_declarations_and_exempt_patterns_read_nothing(self):
        text = ('class AActor;' + NL + 'void Hook(AActor *a);' + NL + 'CCMD(dump) { AActor *mo = players[0].mo; if (mo) { f(mo); } }' + NL +
                'void RenderView(player_t *p) { AActor *x; }' + NL + 'return Make(AActor *b);' + NL)
        out = exempted(text, {'exempt': [r'\bvoid\s+RenderView\s*\([^)]*\)']})
        self.assertEqual(out.count('AActor'), 2)
        self.assertEqual(out.count(NL), text.count(NL))
        self.assertNotIn('players', out)
        self.assertNotIn('player_t', out)

    def test_comments_and_strings_do_not_count(self):
        text = 'a = AActor; // AActor\n/* AActor\nAActor */ s = "AActor";\n'
        stripped = code_only(text)
        self.assertEqual(stripped.count('AActor'), 1)
        self.assertEqual(stripped.count('\n'), text.count('\n'))

    def test_a_rule_covers_its_folders_and_globs(self):
        self.assertTrue(in_set('features/sprites/a.cpp', ['features/sprites/']))
        self.assertTrue(in_set('p_map.cpp', ['p_*.cpp']))
        self.assertFalse(in_set('features/spritesx/a.cpp', ['features/sprites/']))

    def test_matches_are_counted_per_file_outside_its_exclusions(self):
        files = {'features/sprites/a.cpp': 'AActor *a; AActor *b;', 'features/sprites/computation/c_compute.cpp': 'AActor',
                 'features/sprites/a_test.cpp': 'AActor', 'features/sprites/README.md': 'AActor', 'g_game.cpp': 'AActor'}
        self.assertEqual(counts([RENDER], files), {'render': {'features/sprites/a.cpp': 2}})

    def test_copied_fields_are_live_unless_read_through_rc(self):
        rule = dict(RENDER, patterns=[], fields={'table': 'features/gamecopy/fields.h', 'types': ['sector', 'side']})
        files = {'features/gamecopy/fields.h': TABLE,
                 'features/sprites/a.cpp': 'f(sec->floorplane); g(sec->rc.floorplane); h(s.rc->textures); k(side.textures); m(mo->angle);'}
        self.assertEqual([p['name'] for p in field_patterns(rule, files)], ['live floorplane', 'live textures'])
        self.assertEqual(counts([rule], files), {'render': {'features/sprites/a.cpp': 2}})
        self.assertEqual(field_patterns(RENDER, files), [])
        generic = dict(rule, fields=dict(rule['fields'], unpatterned=['textures']))
        self.assertEqual([p['name'] for p in field_patterns(generic, files)], ['live floorplane'])

    def test_reads_through_a_templates_state_selector_are_copy_reads(self):
        rule = dict(RENDER, patterns=[], fields={'table': 'features/gamecopy/fields.h', 'types': ['sector', 'side']})
        body = ('template <class Sel> void F(sector_t *sec, sector_t *d) {\n'
                ' const auto &S = Sel::Of(sec), &D = Sel::Of(f(d));\n'
                ' g(S.floorplane, D.floorplane, Sel::Of(sec->heightsec).floorplane, sec->floorplane, Other::Of(sec).floorplane); }\n'
                'void G(sector_t *sec) { auto &S = Sel::Of(sec); h(S.floorplane); }\n')
        files = {'features/gamecopy/fields.h': TABLE, 'features/sprites/a.cpp': body,
                 'features/sprites/b.cpp': 'void G(sector_t *sec) { auto &S = Sel::Of(sec); h(S.floorplane); }'}
        self.assertEqual(counts([rule], files), {'render': {'features/sprites/a.cpp': 2, 'features/sprites/b.cpp': 1}})

    def test_a_crossing_beyond_its_allowance_fails_and_one_below_asks_to_lower_it(self):
        found = {'render': {'a.cpp': 3, 'b.cpp': 1, 'new.cpp': 1}}
        allow = {'render': {'a.cpp': 2, 'b.cpp': 4, 'gone.cpp': 1}}
        self.assertEqual(check(found, allow), [
            'render: a.cpp crosses 3 times, allowed 2',
            'render: b.cpp crosses 1 times now; lower its allowance from 4 (--update)',
            'render: gone.cpp crosses 0 times now; lower its allowance from 1 (--update)',
            'render: new.cpp crosses 1 times, allowed 0'])
        self.assertEqual(check({'render': {'a.cpp': 2}}, {'render': {'a.cpp': 2}}), [])

    def test_an_update_lowers_allowances_but_never_raises_or_adds_them(self):
        found = {'render': {'a.cpp': 3, 'b.cpp': 1, 'new.cpp': 5}}
        allow = {'render': {'a.cpp': 2, 'b.cpp': 4, 'gone.cpp': 1}}
        self.assertEqual(lowered(found, allow), {'render': {'a.cpp': 2, 'b.cpp': 1}})

    def test_seams_are_listed_once_with_their_owner_outside_tests(self):
        files = {'a.cpp': 'ZX_SEAM("copy/fill", Handoff); ZX_SEAM_UNTIMED("sound/device", Main);',
                 'b.cpp': 'ZX_SEAM("copy/fill", Handoff);', 'a_test.cpp': 'ZX_SEAM("x/y", Main);',
                 'features/handoff/seam.h': 'ZX_SEAM("doc/example", Main)'}
        self.assertEqual(seams(files), [('copy/fill', 'Handoff', 'a.cpp'), ('copy/fill', 'Handoff', 'b.cpp'), ('sound/device', 'Main', 'a.cpp')])

    def test_the_generated_section_lists_the_table_and_the_seams(self):
        rule = dict(RENDER, fields={'table': 'features/gamecopy/fields.h', 'types': ['sector']})
        files = {'features/gamecopy/fields.h': TABLE, 'a.cpp': 'ZX_SEAM("copy/fill", Handoff);'}
        text = doc(files, [rule, RENDER])
        self.assertIn('- sector: `floorplane`', text)
        self.assertIn('- actor: `angle`, `sprite`', text)
        self.assertIn('- actor, worked out by the fill: `who`', text)
        self.assertIn('- player: `viewz`', text)
        self.assertIn('- player, worked out by the fill: `layers`', text)
        self.assertIn('- level: `time`', text)
        self.assertIn('- level, worked out by the fill: `sky1`', text)
        self.assertIn('- decal: `Alpha`', text)
        self.assertIn('- `copy/fill` (Handoff), `a.cpp`', text)

    def test_a_readme_with_markers_is_stale_until_rewritten(self):
        self.assertIsNone(with_doc('no markers', 'x'))
        readme = 'head\n' + DOC_BEGIN + '\nold\n' + DOC_END + '\ntail\n'
        self.assertEqual(with_doc(readme, 'new\n'), 'head\n' + DOC_BEGIN + '\nnew\n' + DOC_END + '\ntail\n')
        files = {'features/handoff/README.md': readme, 'other/README.md': 'plain', 'a.cpp': ''}
        stale = stale_docs(files, [RENDER])
        self.assertEqual([rel for rel, _ in stale], ['features/handoff/README.md'])
        files['features/handoff/README.md'] = stale[0][1]
        self.assertEqual(stale_docs(files, [RENDER]), [])


if __name__ == '__main__':
    unittest.main()

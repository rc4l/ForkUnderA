#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Tests for the boundary tripwire on made-up trees, stdlib only: python3 tools/boundary_tripwire_test.py
import json
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boundary_tripwire import (DOC_BEGIN, DOC_END, check, code_only, counts, doc, exempted, extern_names, field_patterns, in_set,  # noqa: E402
                               lowered, seams, stale_docs, with_doc, write_patterns)

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

    def test_a_renderer_headers_variables_set_by_game_code_count_as_crossings(self):
        rule = {'name': 'game', 'files': ['p_*.cpp'], 'writes': {'headers': ['r_*.h', 'gl/'], 'exclude': ['gl/skip.h'], 'unpatterned': ['validcount']}}
        files = {'r_view.h': ('extern fixed_t viewx, *viewy;' + NL + 'extern const int limit;' + NL + 'extern void R_Do(int a);' + NL +
                              'extern "C" int cfunc;' + NL + 'extern BYTE table[4];' + NL + 'extern int validcount;' + NL + '// extern int commented;' + NL),
                 'gl/gl_x.h': 'extern TArray<FTexture *> glTextures;', 'gl/skip.h': 'extern int skipped;', 'p_x.h': 'extern int gamevar;',
                 'p_a.cpp': ('viewx = 1; a.viewy = 2; p->viewx = 3; if (viewy == 4) {} viewy += 1; ++viewx; table[0] = 1;' + NL +
                             'limit = 2; validcount++; R_Do(1); gamevar = 1; ns::viewx = 5; glTextures = x; if (viewx >= 2 || viewx <= 1) {} viewy--;' + NL +
                             'skipped = 1; viewyz = 1; b = viewx;' + NL)}
        self.assertEqual(extern_names(rule, files), ['cfunc', 'glTextures', 'table', 'viewx', 'viewy'])
        self.assertEqual(counts([rule], files), {'game': {'p_a.cpp': 5}})
        self.assertEqual(write_patterns({'name': 'none', 'files': []}, files), [])
        self.assertEqual(write_patterns(dict(rule, writes={'headers': ['nothing.h']}), files), [])

    def test_the_renderer_rule_counts_a_live_sector_or_side_read_through_its_accessors(self):
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'boundary-rules.json'), encoding='utf-8') as f:
            rule = [r for r in json.load(f)['rules'] if r['name'] == 'renderer-reads-the-copy'][0]
        accessor = re.compile([p for p in rule['patterns'] if p['name'] == 'live map accessor'][0]['regex'])
        live = 'sides[i].GetTexture(part); sec->GetFloorLight(); s->GetTextureXOffset(side_t::mid); f(sector.GetPlaneTexZ(sector_t::floor));'
        copied = 's->rc.GetTexture(side_t::top); sec.rc.GetFloorLight(); rtv->GetTexture(); TexMan.GetTexture(name, use); tex->GetScaledWidth();'
        self.assertEqual(len(accessor.findall(live)), 4)
        self.assertEqual(accessor.findall(copied), [])

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
        files = {'a.cpp': 'ZX_SEAM("copy/fill", Handoff); ZX_SEAM_UNTIMED("sound/device", Main); ZX_SEAM_UNTIMED ("texture/grow", Idle);',
                 'b.cpp': 'ZX_SEAM("copy/fill", Handoff);', 'a_test.cpp': 'ZX_SEAM("x/y", Main);',
                 'features/handoff/seam.h': 'ZX_SEAM("doc/example", Main)'}
        self.assertEqual(seams(files), [('copy/fill', 'Handoff', 'a.cpp'), ('copy/fill', 'Handoff', 'b.cpp'), ('sound/device', 'Main', 'a.cpp'),
                                        ('texture/grow', 'Idle', 'a.cpp')])

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

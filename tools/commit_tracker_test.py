#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Tests for the commit tracker's shared modules (commit-tracker/ledger.py and vcs.py): the
# git and Mercurial log parsers against recorded output, curation surviving a regen, progress to a
# goal row, twin resolution and agreement, row validation, marking, and the backport-notes parser.
#
# Stdlib only, no clone, no Mercurial, no network:  python3 tools/commit_tracker_test.py
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "commit-tracker"))
import ledger as L  # noqa: E402
import overlap  # noqa: E402
import vcs  # noqa: E402

RS, FS, LS = vcs.RS, vcs.FS, vcs.LS

# Recorded from `git log --reverse --format=GIT_LOG_FORMAT --name-only` on UZDoom (TZ=UTC).
GIT_LOG = (
    RS + "455145d611e0707fbf4f6fbcaf9cf7b82cf1b94e" + FS + "a993b0288a0364eafe79c229af162dfc061cc717" + FS
    + "2014-01-01T11:04:32Z" + FS + "1388574272" + FS + "Stall testing for interpolation\n\nsrc/d_net.cpp\nsrc/d_net.h\n"
    + RS + "853c1c4f1ec963bb28ab6e02b7f59c763b2806b8" + FS
    + "6e0f885135b4e562c0355b08aa52ef1a034dcf9f 455145d611e0707fbf4f6fbcaf9cf7b82cf1b94e" + FS
    + "2014-01-01T15:51:27Z" + FS + "1388591487" + FS + "Merge branch 'master' of https://github.com/Edward850/zdoom\n"
    + RS + "49edd7c60c53376a09c385adf841960f71b81662" + FS + "853c1c4f1ec963bb28ab6e02b7f59c763b2806b8" + FS
    + "2014-01-02T06:59:04Z" + FS + "1388645944" + FS + "- Add support\tfor cross compiling\n\nCMakeLists.txt\ngdtoa/CMakeLists.txt\n"
)

# Recorded from `hg log -T HG_LOG_TEMPLATE` on zandronum-stable: a transplant, then a plain commit.
HG_LOG = (
    RS + "26b1948308e990e859a96a81483320cb9e988c4d" + FS
    + "9a59d78038df32a64607c18a7f1308d56e4dbda7 0000000000000000000000000000000000000000" + FS
    + "1410209696 -7200" + FS + "- added option to set a sector's tag via compatibility.txt (needed by GZDoom)" + FS
    + "1af9998fa99334870b458d814c7e3b5cc30fbba6" + FS + "src/compatibility.cpp\n"
    + RS + "697ca180eb436db548171158ab3f0bfef93aa030" + FS
    + "9748c4b3be65c9d383c19fd322285f4f3f10166c 0000000000000000000000000000000000000000" + FS
    + "1754753113 14400" + FS + "- changed the version string to 3.3-alpha" + FS + "" + FS
    + "src/version.h" + LS + "docs/zandronum-history.txt\n"
)

NOTES = """This document contains a list of any out-of-orders commits backported from UZDoom.
===============================================================================================================

===============================================================================================================
Git hash: 7a11be86150422174cd7d745ba5b8fe55d44c686
Mercurial hash: 9761aa28a850960d988e23d1e8de6fe7495c1d39
Fixed dangling links to garbage collected HUD message objects

Reason for documentation: "src/g_statusbar/shared_sbar.cpp" doesn't exist in Zandronum yet, so the node had to
be pasted into "src/g_shared/shared_sbar.cpp" manually instead.
===============================================================================================================

--- src/g_statusbar/shared_sbar.cpp
+++ src/g_statusbar/shared_sbar.cpp
@@ -658,7 +658,6 @@
-	DObject *container = this;
===============================================================================================================
Git hash: 1111111111111111111111111111111111111111
Mercurial hash: 2222222222222222222222222222222222222222
Second entry

Reason for documentation: "a.cpp" and "b.cpp" are missing.
===============================================================================================================

--- a/src/a.cpp
+++ b/src/a.cpp
--- a/src/b.cpp
+++ b/src/b.cpp
"""

UZ = {"name": "uzdoom", "label": "UZDoom", "dir": ".", "repo_url": "https://github.com/UZDoom/UZDoom",
      "commit_url": "https://github.com/UZDoom/UZDoom/commit/{sha}", "goal": {"label": "G", "sha": "tip"},
      "tree": [["wadsrc/", "src/zandronum/wadsrc/"], ["src/", "src/zandronum/src/"]]}
ZA = {"name": "zandronum", "label": "Zandronum", "dir": "zandronum", "twin": "uzdoom", "base_rows": False,
      "repo_url": "https://foss.heptapod.net/z/zs", "commit_url": "https://foss.heptapod.net/z/zs/-/commit/{sha}",
      "goal": {"label": "default", "sha": "tip"}, "tree": [["", "src/zandronum/"]],
      "ignore": ["docs/zandronum-history.txt", ".hgtags"]}

A, B, C, D = ("a" * 40, "b" * 40, "c" * 40, "d" * 40)


def row(sha, status="pending", ours="/", twin=None, note="", nf=None):
    r = {"sha": sha, "date": "2020-01-01T00:00:00Z", "title": "t " + sha[:4], "status": status, "note": note,
         "ours": ours}
    if twin is not None:
        r["twin"] = twin
    r["_nf"] = nf if nf is not None else (7 if twin is not None else 6)
    return r


class Parsers(unittest.TestCase):
    def test_git_log(self):
        cs = vcs.parse_git_log(GIT_LOG)
        self.assertEqual([c.sha[:7] for c in cs], ["455145d", "853c1c4", "49edd7c"])
        self.assertEqual(cs[0].files, ["src/d_net.cpp", "src/d_net.h"])
        self.assertEqual(cs[1].files, [])                      # a merge lists no files
        self.assertEqual(len(cs[1].parents), 2)
        self.assertEqual(cs[2].title, "- Add support for cross compiling")   # tab folded
        self.assertEqual(cs[0].epoch, 1388574272)
        self.assertEqual(cs[0].date, "2014-01-01T11:04:32Z")

    def test_hg_log(self):
        cs = vcs.parse_hg_log(HG_LOG)
        self.assertEqual(len(cs), 2)
        self.assertEqual(cs[0].transplant, "1af9998fa99334870b458d814c7e3b5cc30fbba6")
        self.assertEqual(cs[0].date, "2014-09-08T20:54:56Z")    # hgdate epoch, rendered in UTC
        self.assertEqual(cs[0].parents, ["9a59d78038df32a64607c18a7f1308d56e4dbda7"])   # null parent dropped
        self.assertEqual(cs[1].transplant, "")
        self.assertEqual(cs[1].files, ["src/version.h", "docs/zandronum-history.txt"])

    def test_hg_template_escapes_separators(self):
        t = vcs.hg_template(vcs.HG_LOG_TEMPLATE)
        self.assertNotIn(RS, t)
        self.assertIn(r"\x1e{node}\x1f", t)

    def test_patch_stream(self):
        got = list(vcs.parse_patch_stream("\x01" + A + "\n+x\n-y\n\x01" + B + "\n+z"))
        self.assertEqual(got, [(A, "+x"), (A, "-y"), (B, "+z")])

    def test_grep_git_and_hg(self):
        self.assertEqual(list(vcs.parse_grep("HEAD:src/p.cpp:12:ACSF_Foo=1,", "git")), [("src/p.cpp", "ACSF_Foo=1,")])
        self.assertEqual(list(vcs.parse_grep("src\\p.cpp:12120:5:a:b", "hg")), [("src/p.cpp", "a:b")])


class Rows(unittest.TestCase):
    def test_header_documents_columns(self):
        self.assertIn('"zandronum-base" if baseline', L.header(UZ))
        self.assertIn("<repo>/commit/<sha>", L.header(UZ))
        h = L.header(ZA)
        self.assertIn("<repo>/-/commit/<sha>", h)
        self.assertIn("twin = the uzdoom commit", h)
        self.assertNotIn("zandronum-base", h)

    def test_round_trip(self):
        rows = [row(A, "ported", "abc1234", twin=B, note="n")]
        text = L.format_rows(ZA, rows)
        back = L.parse_rows(text.replace("\n", "\r\n"), L.columns(ZA))     # a CRLF checkout reads the same
        self.assertEqual(back[0]["twin"], B)
        self.assertEqual(back[0]["ours"], "abc1234")
        self.assertEqual(back[0]["_nf"], 7)

    def test_merge_keeps_curation_and_moves_orphans(self):
        cs = vcs.parse_hg_log(HG_LOG)
        old = [row(cs[0].sha, "ported", "abc1234", twin=B, note="kept"),
               row(D, "skip", note="upstream rewrote it away", twin="/"),
               row(C, "pending", twin="/")]
        rows, orphans = L.merge_curation(cs, old, twin_of=lambda c: A if c.transplant else "")
        self.assertEqual(rows[0]["status"], "ported")
        self.assertEqual(rows[0]["note"], "kept")
        self.assertEqual(rows[0]["twin"], B)               # an existing twin is kept, not re-resolved
        self.assertEqual(rows[1]["status"], "pending")     # new commits are born pending
        self.assertEqual(rows[1]["twin"], "/")
        self.assertEqual([o["sha"] for o in orphans], [D])  # curated rows that left history are kept aside

    def test_index_and_files(self):
        cs = vcs.parse_git_log(GIT_LOG)
        idx = L.index_text(cs).splitlines()
        self.assertTrue(idx[0].startswith("# path"))
        self.assertIn("src/d_net.cpp\t455145d611e0707fbf4f6fbcaf9cf7b82cf1b94e", idx)
        files = L.files_text(cs).splitlines()
        self.assertEqual(len(files), 3)                    # header + two commits; the merge has no files
        self.assertTrue(files[1] < files[2])

    def test_ignore_and_tree_map(self):
        self.assertTrue(L.is_ignored(ZA, "docs/zandronum-history.txt"))
        self.assertTrue(L.is_ignored(ZA, ".hgtags"))
        self.assertFalse(L.is_ignored(ZA, "docs/zandronum-history.txt.bak"))
        self.assertEqual(L.our_path(UZ, "src/p_mobj.cpp"), "src/zandronum/src/p_mobj.cpp")
        self.assertEqual(L.our_path(UZ, "wadsrc/static/x.txt"), "src/zandronum/wadsrc/static/x.txt")
        self.assertIsNone(L.our_path(UZ, "libraries/x.c"))
        self.assertEqual(L.our_path(ZA, "protocolspec/spec.txt"), "src/zandronum/protocolspec/spec.txt")


class Progress(unittest.TestCase):
    def test_goal_is_a_row(self):
        led = dict(UZ, goal={"label": "G", "date": "2016-02-23", "sha": B})
        rows = [row(A, "ported", "abc1234"), row(B, "skip"), row(C)]
        p = L.progress(led, rows, "2026-01-01")
        self.assertEqual((p["done"], p["total"], p["goal_sha"]), (2, 2, B))
        self.assertEqual(p["goal_date"], "2016-02-23")
        tip = L.progress(UZ, rows, "x")
        self.assertEqual((tip["done"], tip["total"], tip["pct"]), (2, 3, 66.7))

    def test_unknown_goal_row(self):
        with self.assertRaises(ValueError):
            L.goal_index([row(A)], B)

    def test_text_layout(self):
        p = L.progress(UZ, [row(A, "ported", "abc1234")], "2026-01-01")
        text = L.progress_text(p)
        self.assertTrue(text.startswith('{\n  "goal": "G",\n'))
        self.assertIn('"pct": 100.0', text)

    def test_check_progress_catches_a_stale_file(self):
        rows = [row(A, "ported", "abc1234"), row(B)]
        good = L.progress(UZ, rows, "x")
        self.assertIsNone(L.check_progress(UZ, rows, good))
        rows[1]["status"] = "skip"
        self.assertIn("stale", L.check_progress(UZ, rows, good))


class Twins(unittest.TestCase):
    def commit(self, sha, epoch, title, transplant="x"):
        return vcs.Commit(sha=sha, date="", title=title, epoch=epoch, transplant=transplant)

    def test_resolution(self):
        uz = [(A, 100, "Fix one"), (B, 200, "Fix two"), (C, 200, "Fix three"), (D, 300, "Same"),
              ("e" * 40, 300, "Same")]
        twin_of = L.build_twin_resolver(uz, {"n" * 40: "f" * 40}, order={D: 5, "e" * 40: 9})
        self.assertEqual(twin_of(self.commit("1", 100, "anything")), A)          # unique epoch
        self.assertEqual(twin_of(self.commit("2", 200, "- fix three")), C)       # title breaks the tie
        self.assertEqual(twin_of(self.commit("3", 300, "Same")), D)              # cherry-pick: earlier row
        self.assertEqual(twin_of(self.commit("4", 999, "none")), "")             # no match, no guess
        self.assertEqual(twin_of(self.commit("5", 1, "x", transplant="n" * 40)), "f" * 40)   # notes win
        self.assertEqual(twin_of(self.commit("6", 100, "x", transplant="")), "")              # not a transplant

    def test_agreement(self):
        self.assertTrue(L.twins_agree(row(A, "ported", "abc1234"), row(B, "ported", "abc1234")))
        self.assertFalse(L.twins_agree(row(A, "ported", "abc1234"), row(B, "ported", "def5678")))
        self.assertFalse(L.twins_agree(row(A), row(B, "ported", "abc1234")))
        self.assertFalse(L.twins_agree(row(A), row(B, "skip")))
        self.assertTrue(L.twins_agree(row(A), row(B, "adapted", "abc1234")))      # part not taken
        self.assertTrue(L.twins_agree(row(A, "partially-deferred", "abc1234"), row(B)))

    def test_disagreement_reported(self):
        out = L.twin_disagreements([row(A, "skip", twin=B), row(C, twin="/")], [row(B, "ported", "abc1234")])
        self.assertEqual(len(out), 1)
        self.assertIn("twin disagrees", out[0])

    def test_backport_notes(self):
        es = L.parse_backport_notes(NOTES)
        self.assertEqual(len(es), 2)
        self.assertEqual(es[0]["git"], "7a11be86150422174cd7d745ba5b8fe55d44c686")
        self.assertEqual(es[0]["hg"], "9761aa28a850960d988e23d1e8de6fe7495c1d39")
        self.assertEqual(es[0]["title"], "Fixed dangling links to garbage collected HUD message objects")
        self.assertEqual(es[0]["files"], ["src/g_statusbar/shared_sbar.cpp"])
        self.assertTrue(es[0]["reason"].startswith('"src/g_statusbar/shared_sbar.cpp" doesn\'t exist'))
        self.assertEqual(es[1]["files"], ["src/a.cpp", "src/b.cpp"])

    def test_left_out_round_trip(self):
        es = L.parse_backport_notes(NOTES)
        text = L.left_out_text([row(A, twin="7a11be86150422174cd7d745ba5b8fe55d44c686"), row(B, twin="/")], es)
        lines = text.splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(lines[2].startswith(A + "\t7a11be86"))


class Validation(unittest.TestCase):
    def test_clean(self):
        self.assertEqual(L.validate_rows(UZ, [row(A, "ported", "zandronum-base"), row(B, "skip")]), [])

    def test_rules(self):
        probs = L.validate_rows(UZ, [row(A, "pending", "abc1234"), row(B, "ported", "/"), row(C, "bogus"),
                                     row("xyz"), row(D, nf=5), row(A)])
        text = "\n".join(probs)
        self.assertIn("pending row must have ours=/", text)
        self.assertIn("ours column is empty", text)
        self.assertIn("bad status: bogus", text)
        self.assertIn("bad sha: xyz", text)
        self.assertIn("NF=5", text)
        self.assertIn("duplicate row", text)

    def test_no_base_rows_after_the_base(self):
        probs = L.validate_rows(ZA, [row(A, "ported", "zandronum-base", twin="/")])
        self.assertTrue(any("after our base" in p for p in probs))

    def test_twin_column(self):
        probs = L.validate_rows(ZA, [row(A, twin="nope")], twin_rows=[])
        self.assertTrue(any("bad twin" in p for p in probs))

    def test_tag_pattern(self):
        pat = re.compile(L.tag_pattern("zandronum", ["d09240b3c71cb", "32ff6e13bd13b"]))
        self.assertTrue(pat.search("// [KI] Ported from zandronum@32ff6e13bd13b4c8: x"))
        self.assertTrue(pat.search("zandronum@d09240b"))
        self.assertFalse(pat.search("Ported from qzandronum@32ff6e13bd13"))
        self.assertFalse(pat.search("q-zandronum@d09240b3"))
        self.assertFalse(pat.search("zandronum@32ff6e0"))

    def test_tokens(self):
        self.assertEqual(L.our_tokens("abc1234, def5678"), ["abc1234", "def5678"])
        self.assertEqual(L.our_tokens("zandronum-base"), [])
        self.assertEqual(L.our_tokens("/"), [])


class Mark(unittest.TestCase):
    def test_mark(self):
        rows = [row(A), row(B)]
        r = L.mark(rows, "aaaa", "ported", ours="abc1234", note="why")
        self.assertEqual((r["status"], r["ours"], r["note"]), ("ported", "abc1234", "why"))
        L.mark(rows, "aaaa", "skip", note="more", append_note=True)
        self.assertEqual((rows[0]["ours"], rows[0]["note"]), ("/", "why; more"))   # skip clears ours

    def test_mark_refuses(self):
        rows = [row(A), row(A[:39] + "b")]
        with self.assertRaises(ValueError):
            L.mark(rows, "aaa", "skip")               # ambiguous prefix
        with self.assertRaises(ValueError):
            L.mark([row(B)], "bbbb", "ported")        # needs --ours
        with self.assertRaises(ValueError):
            L.mark([row(B)], "bbbb", "skip", ours="abc1234")
        with self.assertRaises(ValueError):
            L.mark([row(B)], "bbbb", "maybe")


class Overlap(unittest.TestCase):
    def test_mismatch(self):
        rows = [row(A, "ported", "abc1234"), row(B, "ported", "def5678"),
                row(C, "ported", "abc1234", note="no-file-overlap: rewritten into features/"),
                row(D, "ported", "zandronum-base")]
        files = {A: ["src/p_mobj.cpp"], B: ["src/p_user.cpp"], C: ["src/x.cpp"], D: ["src/y.cpp"]}
        ours = {"abc1234": {"p_mobj.cpp"}, "def5678": {"g_level.cpp"}}
        mm, checked = overlap.mismatches(rows, files, files_of=lambda s: ours.get(s, set()))
        self.assertEqual(checked, 2)
        self.assertEqual([m[0] for m in mm], [B])


class Config(unittest.TestCase):
    def test_shipped_config(self):
        cfg = L.load_config()
        self.assertEqual(set(cfg), {"uzdoom", "zandronum"})
        for led in cfg.values():
            for key in ("label", "dir", "vcs", "clone", "from", "to", "commit_url", "repo_url", "goal"):
                self.assertIn(key, led, "%s lacks %s" % (led["name"], key))
            self.assertIn("{sha}", led["commit_url"])
        self.assertEqual(cfg["zandronum"]["twin"], "uzdoom")
        self.assertEqual(L.columns(cfg["zandronum"])[-1], "twin")

    def test_clone_path(self):
        led = {"clone": "../UZDoom", "clone_env": "UZDOOM"}
        self.assertEqual(L.clone_path(led, root="/r/fork", env={}), os.path.normpath("/r/UZDoom"))
        self.assertEqual(L.clone_path(led, root="/r/fork", env={"UZDOOM": "/elsewhere"}), os.path.normpath("/elsewhere"))


if __name__ == "__main__":
    unittest.main(verbosity=1)

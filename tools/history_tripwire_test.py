#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Tests for the history tripwire on throwaway repositories, stdlib and git only: python3 tools/history_tripwire_test.py
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from history_tripwire import check, git, main  # noqa: E402


class HistoryTripwire(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(tmp.cleanup)
        self.tmp = tmp.name
        # [rc4l] An empty global config, so the user's signing, hooks and default branch stay out of the throwaway repositories.
        config = os.path.join(self.tmp, 'gitconfig')
        open(config, 'w').close()
        env = mock.patch.dict(os.environ, GIT_CONFIG_GLOBAL=config, GIT_CONFIG_NOSYSTEM='1', GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t', GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t')
        env.start()
        self.addCleanup(env.stop)
        self.repo = os.path.join(self.tmp, 'repo')
        subprocess.run(['git', 'init', '-q', '-b', 'main', self.repo], check=True)
        self.commit('a')

    def commit(self, message):
        git(self.repo, 'commit', '-q', '--allow-empty', '-m', message)
        return git(self.repo, 'rev-parse', 'HEAD').strip()

    def orphan(self, branch):
        """[rc4l] A branch with a root commit of its own, then back on main."""
        git(self.repo, 'switch', '-q', '--orphan', branch)
        root = self.commit('root of ' + branch)
        git(self.repo, 'switch', '-q', 'main')
        return root

    def test_a_branch_from_base_is_clean(self):
        git(self.repo, 'switch', '-q', '-c', 'feature')
        self.commit('b')
        self.assertEqual(check(self.repo, 'feature', 'main'), [])

    def test_a_base_with_two_roots_is_clean(self):
        self.orphan('other')
        git(self.repo, 'merge', '-q', '--allow-unrelated-histories', '-m', 'join', 'other')
        git(self.repo, 'switch', '-q', '-c', 'feature')
        self.commit('b')
        self.assertEqual(check(self.repo, 'feature', 'main'), [])

    def test_an_unrelated_history_merged_into_a_branch_is_caught(self):
        root = self.orphan('other')
        git(self.repo, 'switch', '-q', '-c', 'feature')
        git(self.repo, 'merge', '-q', '--allow-unrelated-histories', '-m', 'join', 'other')
        self.assertEqual(check(self.repo, 'feature', 'main'), ["feature reaches 1 root commit that main doesn't: " + root[:10]])

    def test_a_branch_of_another_history_is_caught(self):
        roots = sorted([self.orphan('one'), self.orphan('two')])
        git(self.repo, 'switch', '-q', 'one')
        git(self.repo, 'merge', '-q', '--allow-unrelated-histories', '-m', 'join', 'two')
        self.assertEqual(check(self.repo, 'one', 'main'), ["one reaches 2 root commits that main doesn't: %s, %s" % (roots[0][:10], roots[1][:10])])

    def test_a_shallow_checkout_is_refused(self):
        self.commit('b')
        shallow = os.path.join(self.tmp, 'shallow')
        subprocess.run(['git', 'clone', '-q', '--depth', '1', 'file://' + self.repo.replace(os.sep, '/'), shallow], check=True)
        self.assertIn('shallow', check(shallow, 'HEAD', 'HEAD')[0])

    def test_a_base_that_does_not_exist_fails_with_gits_message(self):
        with self.assertRaisesRegex(RuntimeError, 'rev-list --max-parents=0 nowhere'):
            check(self.repo, 'HEAD', 'nowhere')

    def test_main_fails_on_a_base_it_cannot_read(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(main(['history_tripwire.py', 'HEAD', 'refs/nowhere/at-all']), 1)
        self.assertIn('rev-list --max-parents=0 refs/nowhere/at-all', err.getvalue())


if __name__ == '__main__':
    unittest.main()

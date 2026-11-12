#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] History tripwire (python3 tools/history_tripwire.py [head] [base]): a branch reaches only the root commits its base does, so an unrelated or stale copy of the history can't be merged in.
import os
import subprocess
import sys


def git(cwd, *args):
    """[rc4l] A git command's output, raising with git's own message when it fails."""
    run = subprocess.run(['git', '-C', cwd, *args], capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError('git %s: %s' % (' '.join(args), run.stderr.strip()))
    return run.stdout


def roots(cwd, rev):
    """[rc4l] The commits with no parent that rev reaches."""
    return set(git(cwd, 'rev-list', '--max-parents=0', rev).split())


def check(cwd, head, base):
    """[rc4l] What is wrong with head's history against base's, empty when head reaches no root commit base doesn't."""
    if git(cwd, 'rev-parse', '--is-shallow-repository').strip() == 'true':
        return ['the checkout is shallow, so the commits at its cut look like roots; check out with fetch-depth: 0']
    extra = sorted(roots(cwd, head) - roots(cwd, base))
    if not extra:
        return []
    return ["%s reaches %d root commit%s that %s doesn't: %s" % (head, len(extra), '' if len(extra) == 1 else 's', base, ', '.join(c[:10] for c in extra))]


def main(argv):
    head = argv[1] if len(argv) > 1 else 'HEAD'
    base = argv[2] if len(argv) > 2 else 'origin/' + (os.environ.get('GITHUB_BASE_REF') or 'main')
    try:
        problems = check(os.path.dirname(os.path.abspath(__file__)), head, base)
    except RuntimeError as e:
        problems = [str(e)]
    if problems:
        print("HISTORY TRIPWIRE: this branch's history doesn't start where %s's does; re-clone, or move your own commits across with `git rebase --onto %s $(git merge-base --fork-point %s <branch>) <branch>` rather than merging:" % (base, base, base), file=sys.stderr)
        for line in problems:
            print('  ' + line, file=sys.stderr)
        return 1
    print('history-tripwire: clean.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

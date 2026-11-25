#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Runs each test executable ctest knows of once, rather than once per test as ctest does, since starting a sanitized binary for each of four thousand tests was most of the macOS job.
#
# Usage: python3 tests/run_all.py <build dir> [ctest options, e.g. -C Release]
import json
import re
import subprocess
import sys


# [Kaori-Chani] Regex characters CTest's -R (POSIX ERE) would read, escaped so names match
# literally.
ERE_META = re.compile(r'([.^$*+?()\[\]{}|\\])')


def is_gtest(exe, cwd):
    """[Kaori-Chani] Does this program answer --gtest_list_tests with a list of suites?

    The exit status on its own is not enough. A program that takes a number as its first
    argument -- snap_fuzz takes an input count -- reads the flag as zero through strtoul,
    does nothing and succeeds, which would make it look like a test binary that passed.
    So the listing's shape decides: GoogleTest prints one 'Suite.' line per suite.
    """
    try:
        shown = subprocess.run([exe, '--gtest_list_tests'], capture_output=True, text=True,
                               cwd=cwd, timeout=300)
    except (OSError, subprocess.SubprocessError):
        return False
    if shown.returncode != 0:
        return False
    return any(re.match(r'\S+\.\s*$', line) for line in (shown.stdout or '').splitlines())


def executables(listing):
    """[rc4l] Each distinct test command's executable and working directory, in ctest's order."""
    seen, out = set(), []
    for test in listing.get('tests', []):
        command = test.get('command') or []
        if not command or command[0] in seen:
            continue
        seen.add(command[0])
        cwd = next((p['value'] for p in test.get('properties', []) if p.get('name') == 'WORKING_DIRECTORY'), None)
        out.append((command[0], cwd))
    return out


def main(argv):
    if len(argv) < 2:
        sys.exit('usage: run_all.py <build dir> [ctest options]')
    shown = subprocess.run(['ctest', '--test-dir', argv[1], '--show-only=json-v1'] + argv[2:],
                           capture_output=True, text=True, check=True)
    found = executables(json.loads(shown.stdout))
    if not found:
        sys.exit('run_all.py: ctest lists no tests in ' + argv[1])
    failed, standalone = [], []
    for exe, cwd in found:
        # [Kaori-Chani] A program of its own is left to CTest, which knows its arguments.
        if not is_gtest(exe, cwd):
            standalone.append(exe)
            continue
        print('==> ' + exe, flush=True)
        if subprocess.run([exe, '--gtest_brief=1'], cwd=cwd).returncode != 0:
            failed.append(exe)
    if standalone:
        names = [test['name'] for test in json.loads(shown.stdout).get('tests', [])
                 if (test.get('command') or [None])[0] in standalone]
        if names:
            print('==> ctest, for %d test(s) that are programs of their own: %s'
                  % (len(names), ', '.join(names)), flush=True)
            pattern = '^(%s)$' % '|'.join(ERE_META.sub(r'\\\1', n) for n in names)
            if subprocess.run(['ctest', '--test-dir', argv[1], '--output-on-failure',
                               '-R', pattern] + argv[2:]).returncode != 0:
                failed.append('ctest -R ' + pattern)
    for exe in failed:
        print('FAILED: ' + exe)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

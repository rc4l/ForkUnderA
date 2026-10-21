#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Runs each test executable ctest knows of once, rather than once per test as ctest does, since starting a sanitized binary for each of four thousand tests was most of the macOS job.
#
# Usage: python3 tests/run_all.py <build dir> [ctest options, e.g. -C Release]
import json
import subprocess
import sys


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
    failed = []
    for exe, cwd in found:
        print('==> ' + exe, flush=True)
        if subprocess.run([exe, '--gtest_brief=1'], cwd=cwd).returncode != 0:
            failed.append(exe)
    for exe in failed:
        print('FAILED: ' + exe)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

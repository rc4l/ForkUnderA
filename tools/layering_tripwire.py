#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Layering tripwire (python3 tools/layering_tripwire.py): features include each other one way (no cycles), and nothing outside a feature includes a header below its root other than its pure computation/.
import os
import re
import sys
from collections import defaultdict

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
SOURCES = ('.cpp', '.h', '.c', '.mm')


def feature_of(rel):
    """[rc4l] The feature a src-relative path belongs to, or None for the engine."""
    parts = rel.split('/')
    return parts[1] if parts[0] == 'features' and len(parts) > 2 else None


def is_pure(rel):
    """[rc4l] A feature's computation/ is dependency-free, so including it couples nothing."""
    return rel.split('/')[2] == 'computation'


def read_sources(src):
    files = {}
    for root, _, names in os.walk(src):
        for name in names:
            if name.endswith(SOURCES):
                path = os.path.join(root, name)
                rel = os.path.relpath(path, src).replace(os.sep, '/')
                with open(path, encoding='utf-8', errors='replace') as f:
                    files[rel] = f.read()
    return files


def resolve(inc, frm, files):
    """[rc4l] An include as the compiler finds it: src-relative, or beside the including file."""
    beside = os.path.normpath(os.path.join(os.path.dirname(frm), inc)).replace(os.sep, '/')
    for candidate in (inc, beside):
        if candidate in files:
            return candidate
    return None


def includes(files):
    """[rc4l] Every include from outside a feature into it, as (from file, to file), tests left out since they may reach inside."""
    for frm, text in files.items():
        if frm.endswith('_test.cpp'):
            continue
        for inc in INCLUDE.findall(text):
            to = resolve(inc, frm, files)
            if to and feature_of(to) and feature_of(to) != feature_of(frm):
                yield frm, to


def cycles(graph):
    """[rc4l] The strongly connected groups of more than one feature (Tarjan, iterative)."""
    index, low, on_stack, stack, found, counter = {}, {}, set(), [], [], [0]
    for start in sorted(graph):
        if start in index:
            continue
        work = [(start, iter(sorted(graph[start])))]
        index[start] = low[start] = counter[0]
        counter[0] += 1
        stack.append(start)
        on_stack.add(start)
        while work:
            v, edges = work[-1]
            advanced = False
            for w in edges:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    on_stack.add(w)
                    work.append((w, iter(sorted(graph.get(w, ())))))
                    advanced = True
                    break
                if w in on_stack:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                group = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    group.append(w)
                    if w == v:
                        break
                if len(group) > 1:
                    found.append(sorted(group))
    return found


def check(files):
    """[rc4l] The failures: each feature cycle with the includes that close it, then each deep include."""
    graph, why, deep = defaultdict(set), defaultdict(list), []
    for frm, to in includes(files):
        if is_pure(to):
            continue
        if to.split('/')[2:-1]:
            deep.append('%s includes %s, below features/%s\'s root' % (frm, to, feature_of(to)))
        if feature_of(frm):
            graph[feature_of(frm)].add(feature_of(to))
            why[(feature_of(frm), feature_of(to))].append('%s -> %s' % (frm, to))
    problems = []
    for group in cycles(graph):
        problems.append('features include each other in a cycle: ' + ', '.join(group))
        for a in group:
            for b in sorted(graph[a] & set(group)):
                problems.append('  %s -> %s: %s' % (a, b, why[(a, b)][0]))
    return problems + sorted(deep)


def main():
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'zandronum', 'src')
    problems = check(read_sources(src))
    if problems:
        print('LAYERING TRIPWIRE: a feature reaches into another, or two reach into each other; invert one with a listener the higher one installs, or move the shared part down:', file=sys.stderr)
        for line in problems:
            print(line, file=sys.stderr)
        return 1
    print('layering-tripwire: clean.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

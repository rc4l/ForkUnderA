#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Boundary tripwire (python3 tools/boundary_tripwire.py): the renderer reads the game only through features/gamecopy and the game leaves the renderer and UI alone, each rule in tools/boundary-rules.json with an allowance per file in tools/boundary-allow.json that may only shrink; --update lowers allowances to what is left, --init writes them the first time, --doc rewrites the generated boundary sections of the READMEs.
import fnmatch
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layering_tripwire import read_sources  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'src', 'zandronum', 'src')
RULES = os.path.join(HERE, 'boundary-rules.json')
ALLOW = os.path.join(HERE, 'boundary-allow.json')

BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
LINE_COMMENT = re.compile(r'//[^\n]*')
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
FIELD = re.compile(r'^\s*ZX_COPY_FIELD\(\s*(\w+)\s*,\s*(\w+)', re.M)
RECORD_FIELD = re.compile(r'^\s*ZX_(ACTOR|PLAYER|LEVEL|DECAL)_(FIELD|DERIVED)\((?:[^,()]+,)?\s*(\w+)\s*\)', re.M)
SEAM = re.compile(r'ZX_SEAM(?:_UNTIMED)?\s*\(\s*"([^"]+)"\s*,\s*(\w+)\s*\)')
EXTERN = re.compile(r'^[ \t]*extern[ \t]+((?:[\w:<>]+[ \t*&]+)+)([^;(){}]+);', re.M)
CODE = ('.cpp', '.h', '.c', '.mm')
DOC_BEGIN = '<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->'
DOC_END = '<!-- boundary:end -->'


def code_only(text):
    """[rc4l] The text with comments and string literals blanked, line breaks kept so line numbers still hold."""
    blank = lambda m: re.sub(r'[^\n]', ' ', m.group(0))
    return LINE_COMMENT.sub(blank, BLOCK_COMMENT.sub(blank, STRING.sub(blank, text)))


def in_set(rel, patterns):
    """[rc4l] Whether a src-relative path matches any of the rule's globs (a trailing '/' means everything below)."""
    for p in patterns:
        if p.endswith('/') and rel.startswith(p):
            return True
        if fnmatch.fnmatchcase(rel, p):
            return True
    return False


def table_rows(text):
    """[rc4l] Every (kind, member) a field table declares: the map structures' lines, then the actor, player, level and decal records' copied and worked-out members."""
    rows = FIELD.findall(text)
    return rows + [(kind.lower() if what == 'FIELD' else '%s, worked out by the fill' % kind.lower(), name) for kind, what, name in RECORD_FIELD.findall(text)]


def field_patterns(rule, files):
    """[rc4l] For a rule naming a field table, one pattern per copied field: the live name read anywhere but through a copy's rc."""
    spec = rule.get('fields')
    if not spec:
        return []
    table = files.get(spec['table'], '')
    names = sorted({name for kind, name in table_rows(table) if kind in spec['types'] and name not in spec.get('unpatterned', [])})
    return [{'name': 'live %s' % name, 'regex': r'(?<!\brc)(?:\.|->)\s*%s\b' % name} for name in names]


def extern_names(rule, files):
    """[rc4l] For a rule naming `writes` headers, the variables they declare extern, comma lists split, const and `unpatterned` names left out."""
    spec = rule.get('writes')
    if not spec:
        return []
    names = set()
    for rel in files:
        if not rel.endswith('.h') or not in_set(rel, spec['headers']) or in_set(rel, spec.get('exclude', [])):
            continue
        for m in EXTERN.finditer(code_only(files[rel])):
            if re.search(r'\bconst\b', m.group(1)):
                continue
            for part in m.group(2).split(','):
                name = re.match(r'[\s*&]*(\w+)', part)
                if name:
                    names.add(name.group(1))
    return sorted(names - set(spec.get('unpatterned', [])))


def write_patterns(rule, files):
    """[rc4l] For a rule naming `writes` headers, one pattern for any of their variables set, by assignment, compound assignment or increment, other than as some object's member."""
    names = extern_names(rule, files)
    if not names:
        return []
    alt = '|'.join(names)
    regex = r'(?<![\w.>:])(?:%s)\s*(?:<<|>>|[-+*/%%|&^])?=(?!=)|(?:\+\+|--)\s*(?:%s)\b|(?<![\w.>:])(?:%s)\s*(?:\+\+|--)' % (alt, alt, alt)
    return [{'name': 'renderer global written', 'regex': regex}]


def selected(text):
    """[rc4l] The text with reads through a template's state selector (Sel::Of(x).f, or a reference bound to one) spelled as copy reads, since the copy is what the backend's instance selects."""
    for param in set(re.findall(r'\btemplate\s*<\s*(?:class|typename)\s+(\w+)\s*>', text)):
        of = r'\b%s::Of\s*\((?:[^()]|\([^()]*\))*\)' % param
        bound = set(re.findall(r'&\s*(\w+)\s*=\s*' + of, text))
        text = re.sub(of + r'\s*\.', 'rc.', text)
        for name in bound:
            text = re.sub(r'\b%s\s*\.' % name, 'rc.', text)
    return text


CCMD = re.compile(r'\bCCMD\s*\(\s*\w+\s*\)\s*\{')
DECLARATION = re.compile(r'^[ \t]*(?:class|struct)[ \t]+\w+[ \t]*;|^[ \t]*(?:(?!(?:return|else|delete|throw|case|goto|new|do|co_return|co_yield)\b)[\w:<>]+[ \t*&]+)+\w+[ \t]*\([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*\)[ \t]*(?:const[ \t]*)?;', re.M)


def blanked(text, start, end):
    """[rc4l] The text with [start, end) blanked, its line breaks kept."""
    return text[:start] + re.sub(r'[^\n]', ' ', text[start:end]) + text[end:]


def exempted(text, rule):
    """[rc4l] The text without what reads nothing live: console command bodies, which run between tics, declarations, and what the rule's `exempt` patterns name, such as an interface's signatures."""
    for m in reversed(list(CCMD.finditer(text))):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        text = blanked(text, m.start(), i)
    for pattern in [DECLARATION] + [re.compile(p) for p in rule.get('exempt', [])]:
        for m in reversed(list(pattern.finditer(text))):
            text = blanked(text, m.start(), m.end())
    return text


def counts(rules, files):
    """[rc4l] {rule: {file: matches}} for every file each rule covers that matches at all."""
    out = {}
    for rule in rules:
        patterns = [re.compile(p['regex']) for p in rule.get('patterns', []) + field_patterns(rule, files) + write_patterns(rule, files)]
        hits = {}
        for rel in sorted(files):
            if not rel.endswith(CODE) or not in_set(rel, rule['files']) or in_set(rel, rule.get('exclude', [])):
                continue
            text = exempted(selected(code_only(files[rel])), rule)
            n = sum(len(p.findall(text)) for p in patterns)
            if n:
                hits[rel] = n
        out[rule['name']] = hits
    return out


def check(found, allow):
    """[rc4l] The failures: a file crossing a boundary it has no allowance for, crossing it more often, or crossing it less often than its allowance still says."""
    problems = []
    for rule, hits in sorted(found.items()):
        allowed = allow.get(rule, {})
        for rel in sorted(set(hits) | set(allowed)):
            n, a = hits.get(rel, 0), allowed.get(rel, 0)
            if n > a:
                problems.append('%s: %s crosses %d times, allowed %d' % (rule, rel, n, a))
            elif n < a:
                problems.append('%s: %s crosses %d times now; lower its allowance from %d (--update)' % (rule, rel, n, a))
    return problems


def lowered(found, allow):
    """[rc4l] The allowances --update writes: each lowered to what is left, never raised and never added, and zeros dropped."""
    out = {}
    for rule, allowed in allow.items():
        hits = found.get(rule, {})
        kept = {rel: min(a, hits.get(rel, 0)) for rel, a in allowed.items()}
        out[rule] = {rel: n for rel, n in sorted(kept.items()) if n > 0}
    return out


def seams(files):
    """[rc4l] Every declared seam as (name, owner, file), sorted by name."""
    found = set()
    for rel, text in files.items():
        if not rel.endswith(CODE) or rel.endswith('_test.cpp') or rel.endswith('seam.h'):
            continue
        for name, owner in SEAM.findall(text):
            found.add((name, owner, rel))
    return sorted(found)


def doc(files, rules):
    """[rc4l] The generated section: what the copy carries from the field tables, and every seam with its owner."""
    lines = ['## What crosses the boundary', '', 'Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.', '']
    tables = sorted({r['fields']['table'] for r in rules if r.get('fields')})
    for table in tables:
        rows = table_rows(files.get(table, ''))
        lines.append('Copied by `%s`:' % table)
        lines.append('')
        for kind in sorted({k for k, _ in rows}):
            lines.append('- %s: %s' % (kind, ', '.join('`%s`' % n for k, n in rows if k == kind)))
        if not rows:
            lines.append('- nothing yet')
        lines.append('')
    lines.append('Seams:')
    lines.append('')
    for name, owner, rel in seams(files):
        lines.append('- `%s` (%s), `%s`' % (name, owner, rel))
    return '\n'.join(lines) + '\n'


def with_doc(readme, section):
    """[rc4l] A README with its generated section replaced, or None when it has no markers."""
    if DOC_BEGIN not in readme or DOC_END not in readme:
        return None
    head, rest = readme.split(DOC_BEGIN, 1)
    _, tail = rest.split(DOC_END, 1)
    return head + DOC_BEGIN + '\n' + section + DOC_END + tail


def stale_docs(files, rules):
    """[rc4l] The READMEs whose generated section differs from what --doc writes, with the text it would write."""
    section = doc(files, rules)
    out = []
    for rel in sorted(files):
        if not rel.endswith('README.md'):
            continue
        new = with_doc(files[rel], section)
        if new is not None and new != files[rel]:
            out.append((rel, new))
    return out


def read_tree(src):
    files = read_sources(src)
    for root, _, names in os.walk(src):
        for name in names:
            if name == 'README.md':
                path = os.path.join(root, name)
                with open(path, encoding='utf-8', errors='replace') as f:
                    files[os.path.relpath(path, src).replace(os.sep, '/')] = f.read()
    return files


def main(argv):
    with open(RULES, encoding='utf-8') as f:
        rules = json.load(f)['rules']
    files = read_tree(SRC)
    found = counts(rules, files)
    if '--init' in argv:
        if os.path.exists(ALLOW) and '--force' not in argv:
            print('boundary-tripwire: %s exists; --update lowers it, --init --force rewrites it' % ALLOW, file=sys.stderr)
            return 1
        write_json(ALLOW, found)
        print('boundary-tripwire: wrote the allowances for %d crossings' % sum(sum(h.values()) for h in found.values()))
        return 0
    allow = read_json(ALLOW)
    if '--update' in argv:
        write_json(ALLOW, lowered(found, allow))
    if '--doc' in argv:
        for rel, new in stale_docs(files, rules):
            with open(os.path.join(SRC, rel), 'w', encoding='utf-8', newline='\n') as f:
                f.write(new)
            files[rel] = new
    if '--update' in argv:
        allow = read_json(ALLOW)
    problems = check(found, allow) + ['%s: its generated boundary section is out of date (--doc)' % rel for rel, _ in stale_docs(files, rules)]
    if '--report' in argv:
        for rule, hits in sorted(found.items()):
            print('%s: %d crossings in %d files' % (rule, sum(hits.values()), len(hits)))
            for rel, n in sorted(hits.items(), key=lambda kv: -kv[1]):
                print('  %5d  %s' % (n, rel))
    if problems:
        print('BOUNDARY TRIPWIRE: code crosses the game/renderer boundary outside its allowance (tools/boundary-rules.json says why each rule exists):', file=sys.stderr)
        for line in problems:
            print(line, file=sys.stderr)
        return 1
    print('boundary-tripwire: clean, %d allowed crossings left.' % sum(sum(a.values()) for a in allow.values()))
    return 0


def read_json(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def write_json(path, data):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write('\n')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

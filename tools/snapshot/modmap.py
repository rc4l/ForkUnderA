#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Build the snapshot module map: one page showing every source file of the module
# and its tools, the functions and commands each declares, and the calls between them.
#
#   python3 tools/snapshot/modmap.py [-o OUT.html] [--tag TAG]
#
# Run from anywhere; paths are taken from this file's place in the repo. The page is modmap.html
# beside this file with the data filled in: one self-contained file that opens in any browser,
# with nothing to serve. The tag in the page's header is the commit, or what --tag says.
#
# What it reads:
#
#   - The files, grouped into the parts the page shows, from ENTITIES below. A module source
#     that no part claims fails the run, so a new file has to be placed before the map is made.
#   - Each part's description: the Description block of its first file's credit header. Its guide
#     page: the page in docs/net-snapshot/ whose Covers: line names that file.
#   - Public functions: those declared, or defined inline, at the top level of a part's headers
#     or inside their namespace blocks, as doccheck.py reads them; commands are a part's CCMDs,
#     DEV_MODE_CCMD among them, and standalone programs their main or fuzzer entry.
#   - A summary for each: the comment after its declaration on the same line or just above it,
#     else the one at its definition, else the sentence of its guide page naming it or its family
#     (`WriteMover*`) in backticks, a sentence leading into a list taken last.
#   - Calls: the public names called in a function's body and in the bodies of the file-local
#     functions it reaches; a public name used without a call is passed as a pointer.
#   - Engine calls: every function outside the module that calls a namespaced function through
#     netsnapshot::, or a top-level one of the module's by name, counted by site.
#
# Exits 1 if a file is unplaced or a part yields nothing, so a parser gone blind cannot publish
# an empty map.

import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = 'src/zandronum/src'
MODULE = ENGINE + '/features/net-snapshot'
COMP = MODULE + '/computation'
DOCS = 'docs/net-snapshot'
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modmap.html')
CREDIT = 'Copyright (C) 2026 Kaori Ishida'


def comp(stem):
    return [COMP + '/%s.h' % stem, COMP + '/%s.cpp' % stem]


# The parts, by column. Each is (id, files); the first file is the one its description comes from.
COLUMNS = [
    ('server', 'Server', 'once per pump, then per client', [
        ('capture.cpp', [MODULE + '/capture.h', MODULE + '/capture.cpp']),
        ('report.cpp', [MODULE + '/report.h', MODULE + '/report.cpp']),
        ('clienttrack.cpp', [MODULE + '/clienttrack.h', MODULE + '/clienttrack.cpp']),
    ]),
    ('client', 'Client', 'datagrams, ghosts and mirrors', [
        ('apply.cpp', [MODULE + '/apply.h', MODULE + '/apply.cpp']),
        ('entities.cpp', [MODULE + '/entities.h', MODULE + '/entities.cpp']),
        ('ghostactor.cpp', [MODULE + '/ghostactor.h', MODULE + '/ghostactor.cpp']),
        ('localpred.cpp', [MODULE + '/localpred.h', MODULE + '/localpred.cpp']),
    ]),
    ('world', 'Movers and polyobjects', '', [
        ('movers.cpp', [MODULE + '/movers.h', MODULE + '/movers.cpp']),
        ('polyobj.cpp', [MODULE + '/polyobj.h', MODULE + '/polyobj.cpp']),
    ]),
    ('compute', 'Computation', 'pure, tested apart from the game', [
        (s + '.cpp', comp(s)) for s in (
            'budget_compute', 'interest_compute', 'ack_compute', 'join_compute', 'catchup_compute',
            'law_compute', 'clock_compute', 'sample_compute', 'smooth_compute', 'anim_compute',
            'wall_compute', 'sound_compute', 'mover_compute', 'replay_compute',
            'propcensus_compute', 'proptell_compute', 'follow_compute')
    ]),
    ('format', 'Wire format and clock', 'engine-free', [
        ('format.h', [MODULE + '/format.h']),
        ('stream.cpp', [MODULE + '/stream.h', MODULE + '/stream.cpp']),
        ('recordread.cpp', [MODULE + '/recordread.cpp']),
        ('i_tictime.cpp', [ENGINE + '/i_tictime.h', ENGINE + '/i_tictime.cpp',
                           ENGINE + '/i_tictime_wait.cpp', ENGINE + '/i_tictime_web.cpp']),
    ]),
    ('tools', 'Recorder, replay and tests', '', [
        ('record.cpp', [MODULE + '/record.h', MODULE + '/record.cpp']),
        ('csnap_replay.cpp', [MODULE + '/csnap_replay.cpp']),
        ('selftest.cpp', [MODULE + '/selftest.h', MODULE + '/selftest.cpp']),
        ('formattest.cpp', [MODULE + '/formattest.cpp']),
        ('fuzz.cpp', [MODULE + '/fuzz.cpp']),
        ('fuzzgen.h', [MODULE + '/fuzzgen.h']),
        ('snapdump.cpp', ['tests/snapdump/snapdump.h', 'tests/snapdump/snapdump.cpp']),
        ('snapdump/main.cpp', ['tests/snapdump/main.cpp']),
        ('snapfuzz/fuzz.cpp', ['tests/snapfuzz/fuzz.cpp']),
    ]),
]
OPEN = ['capture.cpp', 'apply.cpp']

# Unit tests are not parts: they declare nothing and are reached by no one. A wire test's
# _nettest.cpp is one of them, routed to zandrox_tests_net rather than zandrox_tests.
TEST_RE = re.compile(r'_(net)?test\.cpp$')


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
        return f.read()


failures = []


def fail(msg):
    failures.append(msg)


# ---------------------------------------------------------------- reading C++, as doccheck does

TOKEN_RE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def strip_code(text):
    """Comments, strings and preprocessor lines blanked, offsets and line count kept."""
    def blank(m):
        t = m.group(0)
        return re.sub(r'[^\n]', ' ', t) if t.startswith('/') else t[0] + ' ' * (len(t) - 2) + t[-1]
    out = []
    cont = False
    for line in TOKEN_RE.sub(blank, text).split('\n'):
        if cont or line.lstrip().startswith('#'):
            cont = line.rstrip().endswith('\\')
            out.append(' ' * len(line))
        else:
            out.append(line)
    return '\n'.join(out)


DECL_TAIL = re.compile(r'((?:[A-Za-z_]\w*\s*::\s*)*~?[A-Za-z_]\w*)\s*(\((?:[^()]|\([^()]*\))*\))'
                       r'\s*(?:const)?\s*(?:GCCPRINTF\s*\([^)]*\))?\s*$')
NOT_FUNC = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined', 'GCCPRINTF', 'countof',
            'catch'}
TYPE_START = re.compile(r'(struct|class|union|enum|typedef|using'
                        r'|template\s*<[^>]*>\s*(struct|class))\b')


def line_of(code, offset):
    return code.count('\n', 0, offset) + 1


def top_level(text):
    """Each top-level function of a file, namespace and extern "C" braces not counting as a level:
    name, qualified name, return type, parameters, line, whether in a namespace, and its body's
    span, or None for a declaration."""
    code = strip_code(text)
    items = []
    depth = 0
    opened = []		# per open brace: (counts as a level, is a namespace)
    ns = 0
    stmt_start = 0
    body_open = None
    current = None
    for m in re.finditer(r'[{};]', code):
        ch = m.group(0)
        if ch == '{':
            s = code[stmt_start:m.start()]
            flat = ' '.join(s.split())
            # A macro invocation with no semicolon (CVAR, IMPLEMENT_CLASS) can precede it.
            nsm = re.search(r'(?:^|[\s)])(namespace(?:\s+[\w:]+)?|extern\s*"\s*C\s*")$', flat)
            if depth == 0 and nsm:
                is_ns = nsm.group(1).startswith('namespace')
                opened.append((False, is_ns))
                ns += is_ns
                stmt_start = m.end()
                continue
            if depth == 0:
                current = None
                fm = DECL_TAIL.search(flat)
                if fm and not TYPE_START.match(flat) and '=' not in flat[:fm.start()] \
                        and not re.search(r'\b(struct|class|union|enum)\s+\w+\s*(:[^(]*)?$', flat):
                    q = re.sub(r'\s+', '', fm.group(1))
                    name = q.split('::')[-1]
                    if name not in NOT_FUNC:
                        current = dict(name=name, qual=q, ret=flat[:fm.start()].strip(),
                                       params=' '.join(fm.group(2).split()), ns=ns > 0,
                                       line=line_of(code, stmt_start + len(s) - len(s.lstrip())),
                                       body=None)
                        cm = re.search(r'\b(?:[A-Z]+_)*CCMD\s*\(\s*(\w+)\s*\)$', flat)
                        if cm:
                            current.update(name=cm.group(1), qual=cm.group(1), kind='command')
                        elif name in ('CUSTOM_CVAR', 'CVAR', 'EXTERN_CVAR'):
                            current = None
                body_open = m.end()
            opened.append((True, False))
            depth += 1
            stmt_start = m.end()
        elif ch == '}':
            if opened:
                level, is_ns = opened.pop()
                if level:
                    depth -= 1
                    if depth == 0 and current is not None:
                        current['body'] = (body_open, m.start())
                        items.append(current)
                        current = None
                else:
                    ns -= is_ns
            stmt_start = m.end()
        else:
            if depth == 0:
                flat = ' '.join(code[stmt_start:m.start()].split())
                fm = DECL_TAIL.search(flat)
                if fm and not TYPE_START.match(flat) and '(*' not in flat.replace(' ', '') \
                        and not re.match(r'extern\s+"C"', flat):
                    q = re.sub(r'\s+', '', fm.group(1))
                    name = q.split('::')[-1]
                    if name not in NOT_FUNC and not flat.startswith(('EXTERN_CVAR', 'CVAR')):
                        s = code[stmt_start:m.start()]
                        items.append(dict(name=name, qual=q, ret=flat[:fm.start()].strip(),
                                          params=' '.join(fm.group(2).split()), ns=ns > 0,
                                          line=line_of(code, stmt_start + len(s) - len(s.lstrip())),
                                          body=None))
            stmt_start = m.end()
    return code, items


def trailing_comment(line):
    """The // comment after code on a line, or ''."""
    m = re.match(r'^((?:[^"/]|"(?:\\.|[^"\\])*"|/(?!/))*)//(.*)$', line)
    return m.group(2).strip() if m and m.group(1).strip() else ''


def tidy(s):
    s = s.replace('[Kaori-Chani]', '').strip()
    s = re.sub(r'^features/net-snapshot:\s*', '', s)
    return ' '.join(s.split())


def comment_above(text, line):
    """The comment that documents the declaration starting on `line`: its own trailing comment,
    else the // block above it. A run of one-line declarations or definitions with no comments
    of their own shares the block that heads it, so up to eight such lines are passed over."""
    lines = text.split('\n')
    own = trailing_comment(lines[line - 1])
    if own:
        return tidy(own)
    j = line - 2
    passed = 0
    while j >= 0 and passed < 8:
        t = lines[j].strip()
        one_liner = t.endswith('}') and t.count('{') == t.count('}') > 0
        if not t or t.startswith(('//', '#')) or trailing_comment(t) or \
                not (one_liner or (t.endswith(';') and '{' not in t)):
            break
        j -= 1
        passed += 1
    got = []
    while j >= 0:
        t = lines[j].strip()
        if not t.startswith('//') or re.match(r'//\s*[*=\-]{4,}', t) or re.match(r'//\s*$', t):
            break
        got.append(t[2:].strip())
        j -= 1
    got.reverse()
    return tidy(' '.join(got))


def clean_ret(ret):
    ret = re.sub(r'\b(inline|static|extern|ZX_[A-Z_]+)\b', '', ret)
    ret = re.sub(r'template\s*<[^>]*>', '', ret)
    return ' '.join(ret.split())


def description(text):
    m = re.search(r'^//\s*Description:\s*(.*?)\n//\s*\n', text, re.M | re.S)
    if not m:
        return ''
    return ' '.join(re.sub(r'^//\s?', '', l) for l in m.group(1).split('\n')).strip()


CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
WORD_RE = re.compile(r'\b([A-Za-z_]\w*)\b(?!\s*\()')


# ---------------------------------------------------------------- the parts

placed = {}
for col in COLUMNS:
    for eid, files in col[3]:
        for f in files:
            placed[f] = eid
            if not os.path.isfile(os.path.join(ROOT, f)):
                fail('%s: %s is not in the repo' % (eid, f))

for base, dirs, names in os.walk(os.path.join(ROOT, MODULE)):
    dirs[:] = [d for d in dirs if d != 'testdata']
    for n in names:
        rel = os.path.relpath(os.path.join(base, n), ROOT).replace(os.sep, '/')
        if n.endswith(('.cpp', '.h')) and not TEST_RE.search(n) and rel not in placed:
            fail('%s is in the module but in no part of the map; place it in COLUMNS' % rel)

pages = {}
docs_dir = os.path.join(ROOT, DOCS)
for n in sorted(os.listdir(docs_dir)) if os.path.isdir(docs_dir) else []:
    if not n.endswith('.md'):
        continue
    t = read(DOCS + '/' + n)
    m = re.search(r'^Covers:(.*)$', t, re.M)
    if m:
        for c in m.group(1).split(','):
            pages.setdefault(c.strip().strip('`'), n)

texts, codes, tops = {}, {}, {}
for f in placed:
    if os.path.isfile(os.path.join(ROOT, f)):
        texts[f] = read(f)
        codes[f], tops[f] = top_level(texts[f])

entities, funcs = {}, {}
public_ns = {}		# name -> True when declared inside a namespace
entry_names = {'main', 'LLVMFuzzerTestOneInput'}

for col in COLUMNS:
    for eid, files in col[3]:
        files = [f for f in files if f in texts]
        if not files:
            continue
        guide = next((pages[f] for f in files if f in pages), '')
        entities[eid] = dict(id=eid, files=[f[len(ENGINE) + 1:] if f.startswith(ENGINE + '/') else f
                                            for f in files],
                             layer=col[0], page=guide, resp=description(texts[files[0]]),
                             lines=sum(texts[f].count('\n') for f in files))
        if not entities[eid]['resp']:
            fail('%s: no Description block in %s' % (eid, files[0]))

# Where each name is defined, among the placed files.
defined = {}
for f in texts:
    for it in tops[f]:
        if it['body'] is not None and it.get('kind') != 'command':
            defined.setdefault(it['name'], []).append((f, it))

# A public function belongs to the part that defines it, which is its header's part unless the body
# lives in another (record.h's reader is recordread.cpp's).
decls = {}
for f in texts:
    if not f.endswith('.h'):
        continue
    for it in tops[f]:
        if it.get('kind') is None:
            decls.setdefault(it['name'], (f, it))
for name, (f, it) in decls.items():
    defs = defined.get(name, [])
    home = [d for d in defs if placed[d[0]] == placed[f]] or defs
    eid = placed[home[0][0]] if home else placed[f]
    funcs[name] = dict(n=name, e=eid, f='', h=f.split('/')[-1], l=it['line'], sig=it['params'],
                       ret=clean_ret(it['ret']), s='', src='', c=[], r=[], k='function',
                       _decl=(f, it), _defs=home)
    public_ns[name] = it['ns']
for f in texts:
    for it in tops[f]:
        if it.get('kind') == 'command' or (it['name'] in entry_names and it['body'] is not None):
            k = 'command' if it.get('kind') == 'command' else 'entry'
            key = it['name'] if k == 'command' else '%s (%s)' % (it['name'], placed[f])
            funcs[key] = dict(n=it['name'], e=placed[f], f='', h='', l=it['line'], sig='',
                              ret='', s='', src='', c=[], r=[], k=k, _decl=(f, it),
                              _defs=[(f, it)])
for eid in entities:
    if not any(fn['e'] == eid for fn in funcs.values()):
        fail('%s: no public function, command or entry found; the parser has gone blind' % eid)

pub = set(f['n'] for f in funcs.values() if f['k'] == 'function')


# ---------------------------------------------------------------- bodies, summaries and calls

def body_names(code, span):
    b = code[span[0]:span[1]]
    calls = set(CALL_RE.findall(b))
    words = set(WORD_RE.findall(b))
    return calls, words - calls


for key, fn in funcs.items():
    f, decl = fn.pop('_decl')
    defs = fn.pop('_defs')
    eid = fn['e']
    if defs:
        fn['f'] = defs[0][0].split('/')[-1]
        fn['l'] = defs[0][1]['line']
    else:
        fn['f'] = f.split('/')[-1]
    # Summary.
    s = comment_above(texts[f], decl['line']) if f.endswith('.h') else ''
    if s:
        fn['src'] = 'comment'
    else:
        for x, it in defs:
            s = comment_above(texts[x], it['line'])
            if s:
                fn['src'] = 'def' if fn['k'] == 'function' else 'comment'
                if fn['src'] == 'comment':
                    fn['h'] = x.split('/')[-1]
                break
    if not s and entities[eid]['page']:
        page = read(DOCS + '/' + entities[eid]['page'])
        blocks = ['']
        for l in page.split('\n'):
            item = re.match(r'^\s*(?:[-*]|\d+\.)\s+', l)
            if not l.strip() or l.startswith(('#', 'Covers:', '|', '```')) or item:
                blocks.append('')
            if l.strip() and not l.startswith(('#', 'Covers:', '|', '```')):
                blocks[-1] += ' ' + (l[item.end():] if item else l)
        # The sentence naming it, or the family it belongs to (`WriteMover*`), and one that
        # leads into a list (ending in a colon) only when nothing else names it.
        best = None
        for sent in (x for blk in blocks for x in re.split(r'(?<=\.)\s+(?=[A-Z`(])', blk)):
            sent = ' '.join(sent.split())
            named = re.search(r'`%s`' % re.escape(fn['n']), sent)
            family = any(fn['n'].startswith(p) for p in re.findall(r'`([A-Za-z_]\w*)\*`', sent))
            if not (named or family):
                continue
            rank = (sent.endswith(':'), not named)
            if best is None or rank < best[0]:
                best = (rank, sent)
        if best:
            s, fn['src'] = best[1], 'guide'
    fn['s'] = s
    # Calls, through the file-local helpers each body reaches.
    calls, refs = set(), set()
    for x, it in defs:
        local = {i['name']: i for i in tops[x] if i['body'] is not None and i['name'] not in pub}
        seen, todo = set(), [it]
        while todo:
            cur = todo.pop()
            c, r = body_names(codes[x], cur['body'])
            calls |= c
            refs |= r
            for n in c | r:
                if n in local and n not in seen:
                    seen.add(n)
                    todo.append(local[n])
    fn['c'] = sorted((calls & pub) - {fn['n']})
    fn['r'] = sorted((refs & pub) - calls - {fn['n']})


# ---------------------------------------------------------------- calls from the engine

engine = {}
NS_CALL = re.compile(r'\bnetsnapshot\s*::\s*([A-Za-z_]\w*)')
top_pub = set(n for n in pub if not public_ns.get(n))
for base, dirs, names in os.walk(os.path.join(ROOT, ENGINE)):
    dirs[:] = [d for d in dirs if d not in ('_deps', 'build')]
    for n in names:
        rel = os.path.relpath(os.path.join(base, n), ROOT).replace(os.sep, '/')
        if not n.endswith(('.cpp', '.h', '.c')) or rel in placed or rel.startswith(MODULE + '/') \
                or TEST_RE.search(n):
            continue
        text = read(rel)
        if 'netsnapshot' not in text and 'i_tictime.h' not in text \
                and 'features/net-snapshot/' not in text:
            continue
        code, items = top_level(text)
        for it in items:
            if it['body'] is None:
                continue
            b = code[it['body'][0]:it['body'][1]]
            hits = {}
            for m in NS_CALL.finditer(b):
                if m.group(1) in pub and public_ns.get(m.group(1)):
                    hits.setdefault(m.group(1), []).append(line_of(code, it['body'][0] + m.start()))
            for m in CALL_RE.finditer(b):
                nm = m.group(1)
                if nm in top_pub and not b[:m.start()].rstrip().endswith(('::', '.', '->')):
                    hits.setdefault(nm, []).append(line_of(code, it['body'][0] + m.start()))
            if hits:
                eng_id = rel[len(ENGINE) + 1:]
                caller = it['qual']
                if it.get('kind') == 'command':
                    caller = 'CCMD ' + it['name']
                elif re.match(r'^[A-Z][A-Z0-9_]*$', caller):
                    # A macro that defines a function names it last, as in
                    # DEFINE_ACTION_FUNCTION(AActor, A_X).
                    words = re.findall(r'[A-Za-z_]\w*', it['params'])
                    caller = words[-1] if words else caller
                # Overloads of one name share a row.
                row = engine.setdefault(eng_id, {'id': eng_id, 'callers': {}})['callers'] \
                    .setdefault(caller, {})
                for k, v in hits.items():
                    row[k] = sorted(row.get(k, []) + v)

if not engine:
    fail('no engine file calls into the module; the hook scan has gone blind')


# ---------------------------------------------------------------- the page

def tag():
    try:
        h = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'],
                           capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(['git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=no'],
                               capture_output=True, text=True).stdout.strip()
        return h + (' plus local changes' if dirty else '')
    except (OSError, subprocess.CalledProcessError):
        return 'a working tree'


def main():
    ap = argparse.ArgumentParser(description='Build the snapshot module map page.')
    ap.add_argument('-o', '--out', default='snapshot-module-map.html')
    ap.add_argument('--tag', help='what the header names as the source, instead of the commit')
    a = ap.parse_args()

    for msg in failures:
        print('modmap: ' + msg, file=sys.stderr)
    if failures:
        return 1

    data = dict(entities=entities, funcs=funcs, engine=engine, tag=a.tag or tag(),
                open=[x for x in OPEN if x in entities],
                cols=[dict(id=c[0], title=c[1], note=c[2], layer=c[0],
                           ents=[e for e, _ in c[3] if e in entities]) for c in COLUMNS])
    blob = json.dumps(data, separators=(',', ':'), sort_keys=True).replace('</', '<\\/')
    with open(TEMPLATE, encoding='utf-8') as f:
        page = f.read()
    if page.count('@MODMAP_DATA@') != 1:
        print('modmap: %s has no single @MODMAP_DATA@ slot' % TEMPLATE, file=sys.stderr)
        return 1
    page = page.replace('@MODMAP_DATA@', blob)
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(page)

    nf = sum(1 for x in funcs.values() if x['k'] == 'function')
    sites = sum(len(v) for e in engine.values() for c in e['callers'].values() for v in c.values())
    nosum = sum(1 for x in funcs.values() if not x['s'])
    print('modmap: %d parts, %d public functions, %d commands and entries, %d engine files, '
          '%d engine call sites, %d without a summary -> %s'
          % (len(entities), nf, len(funcs) - nf, len(engine), sites, nosum, a.out))
    return 0


if __name__ == '__main__':
    sys.exit(main())

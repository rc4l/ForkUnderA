#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Check the maintainers guide (docs/net-snapshot/) against the code.
#
#   python3 tools/snapshot/doccheck.py
#
# Run from anywhere; paths are taken from this file's place in the repo. Exits 1 on any failure.
# What it checks, per guide page (a .md file in docs/net-snapshot/ other than the design doc):
#
#   - Every name a page writes in backticks exists: an identifier in the C++ code outside comments
#     and strings, in a preprocessor line, or in a Python or build file; a file in the repo; or a
#     setting or command declared in the code. A retired or renamed setting (csnap_replay.cpp's
#     tables) passes only on a line that says "retired" or "renamed".
#   - `PREFIX*` (N) names a family: the trailing * comes after a prefix, N is how many public
#     functions of the page's headers start with it, at least 2, and the count must match.
#   - Each link into the design doc names one of its headings, and each link to a page exists.
#
# And for the source files a page claims on its "Covers:" line:
#
#   - every public function (declared, or defined inline, at the top level of a covered header or
#     inside its namespace blocks) is named on the page or in a family, unless ALLOWED gives a
#     reason;
#   - every setting and command (CVAR, CUSTOM_CVAR, CCMD) is named: all of them in a module-owned
#     file; in a stock file those with a snapshot prefix or a [Kaori-Chani] tag on their line or in
#     the comment block just above;
#   - every stock file that calls a covered header's function is named;
#   - a page covering entities.h names each installer hook (a Set* taking and returning a Clent*Fn)
#     and each file that calls one.
#
# A covered header yielding no public function fails, as does finding no installer hook, so a
# lookup the code has outgrown cannot pass by finding nothing.
#
# Module-owned files are those opening with the 2026 credit header. One that no page covers is a
# failure, or a warning with STAGED set.
#
# And across the repo:
#
#   - no pointer to a note kept outside it: a name ending in -note, a path into the claude
#     folder of the project the notes live in, or the prose forms that name those notes, the
#     recheck file or the merge plan;
#   - every .md a comment names exists, since a page renamed leaves citations pointing nowhere;
#   - none of the source tree's own file or unit names in a module comment, the port having
#     renamed every one of them;
#   - no `file.cpp:NNN` line citation in a page, a module file or the module's README, since line
#     numbers move with every edit and a citation by function does not.
#
# These are checked on every page and the design doc, every line of a module-owned file, and every
# [Kaori-Chani] block in a stock source or protocol spec file.

import os
import re
import sys

STAGED = False

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, 'docs', 'net-snapshot')
DESIGN = 'design.md'
ENGINE = 'src/zandronum/src'
MODULE = ENGINE + '/features/net-snapshot'
CODE_DIRS = (ENGINE, 'tools/snapshot', 'tests')
CODE_EXT = ('.cpp', '.h', '.c', '.py', '.txt')
CREDIT = 'Copyright (C) 2026 Kaori Ishida'
TAG = '[Kaori-Chani]'
SETTING_PREFIX = re.compile(r'^(fua_snap(_|$)|fua_csnap_)')

# The source tree's names, every one of them renamed by the port: a module comment naming one is
# pointing at a file or a unit this repo does not have.
OLD_NAMES = re.compile(r'\b(?:sv_snapshot|sv_snapclient|cl_snapshot|cl_entities|cl_ghostactor|'
                       r'cl_record|cl_recordread|cl_replay|snap_movers|snap_polyobj|snap_format|'
                       r'snap_stream|snap_fixture|snap_fuzz|snap_fuzzgen|snap_formattest)'
                       r'\.(?:cpp|h|md)\b'
                       r'|\bsnap_\w+_compute\b|\bsnap_tools/|\bsnap_docs/')

# Public functions deliberately left off a page, each with its reason.
ALLOWED = {
}

failures = []
warnings = []


def fail(page, msg):
    failures.append('%s: %s' % (page, msg))


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


TOKEN_RE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def strip_code(text):
    """Comments, strings and preprocessor lines blanked, line count kept."""
    def blank(m):
        t = m.group(0)
        if t.startswith('/'):
            return '\n' * t.count('\n')
        return t[0] * 2
    lines = []
    cont = False
    for line in TOKEN_RE.sub(blank, text).split('\n'):
        if cont or line.lstrip().startswith('#'):
            cont = line.rstrip().endswith('\\')
            lines.append('')
        else:
            lines.append(line)
    return '\n'.join(lines)


# ---------------------------------------------------------------- the code

code_files = {}
for d in CODE_DIRS:
    for base, dirs, names in os.walk(os.path.join(ROOT, d)):
        dirs[:] = [x for x in dirs if x not in ('_deps', 'build', 'vcpkg')]
        for n in names:
            if n.endswith(CODE_EXT):
                p = os.path.join(base, n)
                code_files[os.path.relpath(p, ROOT).replace(os.sep, '/')] = read(p)

credited = set(p for p, t in code_files.items()
               if re.search(r'^\s*(//|#).*' + re.escape(CREDIT), t[:2000], re.M))
# The credit is an author's, and this author's work reaches other features too; the guide answers
# for the module, so only its own files must be covered by a page.
module_owned = set(p for p in credited if p.startswith(MODULE + '/'))
owned = credited
stripped = {p: strip_code(t) for p, t in code_files.items() if p.endswith(('.cpp', '.h', '.c'))}

WORD_RE = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')

# What "in the code" means: identifiers in C++ code outside comments and strings, the names
# preprocessor lines use, and the Python and build files.
identifiers = set()
file_words = {}
for p, t in code_files.items():
    if p in stripped:
        file_words[p] = set(WORD_RE.findall(stripped[p]))
        identifiers |= file_words[p]
        for line in t.split('\n'):
            if line.lstrip().startswith('#'):
                identifiers.update(WORD_RE.findall(line.split('//')[0]))
    else:
        identifiers.update(WORD_RE.findall(t))

# Words inside string literals, which is where a command keyword, a file the code writes and a
# function named only in a message live; a page may write any of them.
quoted = set()
for p, t in code_files.items():
    for lit in re.findall(r'"((?:\\.|[^"\\\n])*)"', t):
        quoted.update(re.findall(r'[A-Za-z_][\w.-]*', lit))

all_paths = set(code_files)
for base, dirs, names in os.walk(ROOT):
    dirs[:] = [x for x in dirs
               if (x.startswith('.') is False or x == '.github')
               and x not in ('build-win', 'build-tests')]
    for n in names:
        all_paths.add(os.path.relpath(os.path.join(base, n), ROOT).replace(os.sep, '/'))
basenames = set(os.path.basename(p) for p in all_paths)

SETTING_RE = re.compile(r'^\s*(?:CUSTOM_CVAR|CVAR)\s*\(\s*\w+\s*,\s*(\w+)'
                        r'|^\s*CCMD\s*\(\s*(\w+)', re.M)


def settings_in(path, stock):
    lines = code_files[path].split('\n')
    found = []
    for i, line in enumerate(lines):
        m = SETTING_RE.match(line)
        if not m:
            continue
        name = m.group(1) or m.group(2)
        if stock and not SETTING_PREFIX.match(name):
            tagged = TAG in line
            j = i - 1
            while j >= 0 and lines[j].strip().startswith('//') and not tagged:
                tagged = TAG in lines[j]
                j -= 1
            if not tagged:
                continue
        found.append(name)
    return found


declared_settings = set()
for p in code_files:
    if p.endswith('.cpp'):
        for m in SETTING_RE.finditer(code_files[p]):
            declared_settings.add(m.group(1) or m.group(2))

retired = set()
replay = code_files.get(MODULE + '/csnap_replay.cpp', '')
for table in ('g_replayRetired', 'g_replayRenamed'):
    m = re.search(table + r'\[\]\s*=\s*\{(.*?)\};', replay, re.S)
    if m:
        retired.update(re.findall(r'\{\s*"(\w+)"', m.group(1)))

DECL_TAIL = re.compile(r'\b([A-Za-z_]\w*)\s*\((?:[^()]|\([^()]*\))*\)\s*(?:const)?\s*'
                       r'(?:GCCPRINTF\s*\([^)]*\))?\s*$')
NOT_FUNC = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined', 'GCCPRINTF', 'countof'}


def public_functions(path):
    """Names declared or defined inline at the top level of a header, a namespace block's braces
    not counting as a level."""
    names = []
    depth = 0
    opened = []
    stmt = ''
    for tok in re.split(r'([{};])', stripped[path]):
        if tok == '{':
            s = ' '.join(stmt.split())
            if depth == 0 and re.match(r'namespace\b', s):
                opened.append(False)
                stmt = ''
                continue
            if depth == 0:
                m = DECL_TAIL.search(s)
                if m and not re.match(r'(struct|class|union|enum|typedef)\b', s) \
                        and not re.search(r'\b(struct|class|union|enum)\s+\w+\s*(:[^(]*)?$', s):
                    names.append(m.group(1))
            opened.append(True)
            depth += 1
            stmt = ''
        elif tok == '}':
            if opened and opened.pop():
                depth -= 1
            stmt = ''
        elif tok == ';':
            if depth == 0:
                s = ' '.join(stmt.split())
                if not re.match(r'(typedef|using|extern\s+"C")\b', s) \
                        and '(*' not in s.replace(' ', ''):
                    m = DECL_TAIL.search(s)
                    if m:
                        names.append(m.group(1))
            stmt = ''
        else:
            stmt += tok
    return [n for n in names if n not in NOT_FUNC]


headers = {p: public_functions(p) for p in owned if p.endswith('.h') and p in stripped}
for p in sorted(headers):
    if not headers[p]:
        fail(os.path.basename(p), 'no public function found; the header parser has gone blind')


#   A call, not a mention: the same word in another feature's file is not a caller. Taken once,
# name by name, since a scan per function over the engine's thousands of files is minutes.
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
callers_of = {}
for p, t in stripped.items():
    if (p in module_owned) or (p.startswith(ENGINE + '/') is False):
        continue
    # A file that includes none of the module's headers cannot be calling into it, whatever names
    # of its own it shares with them.
    if 'features/net-snapshot/' not in code_files[p]:
        continue
    for n in set(CALL_RE.findall(t)):
        callers_of.setdefault(n, set()).add(p)


def stock_callers(name):
    return callers_of.get(name, set())


# Installer hooks: Set* taking and returning a Clent*Fn.
installers = []
ent = stripped.get(MODULE + '/entities.h', '')
for m in re.finditer(r'\b(Clent\w*Fn)\s+(Set\w+)\s*\(\s*\1\b', ent):
    installers.append(m.group(2))
if not installers:
    fail('entities.h', 'no installer hook found; the pattern has gone blind')


def installer_callers(name):
    pat = re.compile(r'\b%s\b' % re.escape(name))
    defn = re.compile(r'\bClent\w*Fn\s+%s\s*\(' % re.escape(name))
    return set(p for p, t in stripped.items()
               if pat.search(t) and not defn.search(t))


# ---------------------------------------------------------------- the design doc

def slug(heading):
    s = heading.strip().lower()
    s = re.sub(r'[^\w\- ]', '', s, flags=re.U)
    return s.replace(' ', '-')


design_path = os.path.join(DOCS, DESIGN)
slugs = set()
if os.path.exists(design_path):
    seen = {}
    for line in read(design_path).split('\n'):
        m = re.match(r'^#{1,6}\s+(.*?)\s*#*\s*$', line)
        if m:
            s = slug(m.group(1))
            n = seen.get(s, 0)
            seen[s] = n + 1
            slugs.add(s if n == 0 else '%s-%d' % (s, n))
else:
    failures.append('%s: missing' % DESIGN)


# ---------------------------------------------------------------- the pages

pages = sorted(n for n in os.listdir(DOCS) if n.endswith('.md') and n != DESIGN) \
    if os.path.isdir(DOCS) else []
covered_by = {}

for page in pages:
    text = read(os.path.join(DOCS, page))
    lines = text.split('\n')

    covers = []
    for line in lines:
        if line.startswith('Covers:'):
            covers = [c.strip().strip('`') for c in line[len('Covers:'):].split(',') if c.strip()]
            break
    for c in covers:
        if c not in code_files:
            fail(page, 'covers %s, which is not in the repo' % c)
        else:
            covered_by.setdefault(c, []).append(page)
    covers = [c for c in covers if c in code_files]

    page_funcs = []
    for c in covers:
        page_funcs += headers.get(c, [])

    named = set()
    for i, line in enumerate(lines, 1):
        for tok in re.findall(r'`([^`]+)`', line):
            tok = tok.strip()
            if ('*' in tok) and (('/' in tok) or ('.' in tok)):
                continue	# a glob, as the build files read it
            fam = re.match(r'^([A-Za-z_]\w*)\*$', tok)
            if fam:
                rest = line[line.index('`%s`' % tok) + len(tok) + 2:]
                cnt = re.match(r'\s*\((\d+)\)', rest)
                members = [f for f in set(page_funcs) if f.startswith(fam.group(1))]
                if not cnt:
                    fail(page, 'line %d: family `%s` has no (count)' % (i, tok))
                elif len(members) < 2 or int(cnt.group(1)) != len(members):
                    fail(page, 'line %d: family `%s` (%s) matches %d of the page\'s functions'
                         % (i, tok, cnt.group(1), len(members)))
                named.update(members)
                continue
            if '*' in tok and ' ' not in tok:
                fail(page, 'line %d: `%s`: a family is a prefix and a trailing *' % (i, tok))
                continue
            if re.match(r'^[A-Za-z_]\w*_$', tok):
                continue	# a settings prefix, named as one
            ident = re.match(r'^([A-Za-z_][\w:]*)(\(\))?$', tok)
            if ident:
                name = ident.group(1).split('::')[-1]
                named.add(name)
                if name in retired and name not in declared_settings:
                    low = line.lower()
                    if 'retired' not in low and 'renamed' not in low:
                        fail(page, 'line %d: `%s` is retired or renamed; say so on its line'
                             % (i, name))
                elif SETTING_PREFIX.match(name) and name not in declared_settings:
                    if name not in identifiers:
                        fail(page, 'line %d: no setting or command `%s`' % (i, name))
                elif (name not in identifiers) and (name not in quoted):
                    fail(page, 'line %d: `%s` is not in the code' % (i, name))
                continue
            if re.match(r'^[\w./-]+\.(cpp|h|c|py|md|txt|yml|csnap|zsnap)$', tok):
                base = os.path.basename(tok)
                # A recording or a log the code writes is named in the code, not in the repo.
                if base not in basenames and base not in quoted \
                        and not tok.endswith(('.csnap', '.zsnap')):
                    fail(page, 'line %d: no file `%s`' % (i, tok))
                continue
            # Anything else (a command line, an expression) is prose.

        for target, anchor in re.findall(r'\]\(([^)#\s]*)#([^)\s]+)\)', line):
            if os.path.basename(target) == DESIGN and anchor not in slugs:
                fail(page, 'line %d: no design doc heading for #%s' % (i, anchor))
        for target in re.findall(r'\]\(([^)#\s]+\.md)(?:#[^)\s]*)?\)', line):
            if not os.path.exists(os.path.join(DOCS, target)) \
                    and not os.path.exists(os.path.join(DOCS, target.lstrip('./'))) \
                    and not os.path.exists(os.path.join(ROOT, target.lstrip('./'))):
                fail(page, 'line %d: link to missing page %s' % (i, target))

    for c in covers:
        stock = c not in owned
        for f in headers.get(c, []):
            if f not in named and f not in ALLOWED:
                fail(page, '%s declares %s, which the page does not name' % (c, f))
            if not stock:
                for caller in sorted(stock_callers(f)):
                    if os.path.basename(caller) not in text:
                        fail(page, '%s calls %s, and the page does not name %s'
                             % (caller, f, os.path.basename(caller)))
        for s in settings_in(c, stock):
            if s not in named:
                fail(page, '%s declares setting %s, which the page does not name' % (c, s))
        if c == MODULE + '/entities.h':
            for h in installers:
                if h not in named:
                    fail(page, 'installer hook %s is not named' % h)
                for caller in sorted(installer_callers(h)):
                    if caller != MODULE + '/entities.h' \
                            and os.path.basename(caller) not in text:
                        fail(page, '%s installs %s, and the page does not name it'
                             % (os.path.basename(caller), h))

for p in sorted(module_owned):
    if p not in covered_by:
        (warnings if STAGED else failures).append('%s: no guide page covers it' % p)
for name in ALLOWED:
    if not any(name in fs for fs in headers.values()):
        failures.append('ALLOWED: %s is not a public function any more' % name)

# ---------------------------------------------------------------- notes, pages and old names

# Each alternative is spelt with a class so this file does not match itself. `the design doc`
# is not among them, naming design.md, which is in the repo.
NOTE_RE = re.compile(r"[A-Za-z0-9]-note\b|\bclaude[/]|port'?[s] (own )?note[s]?\b"
                     r"|recheck [d]ocument|fua-merge-[p]lan")
LINEREF_RE = re.compile(r'\b[\w./-]+\.(?:cpp|h|c|py)[:]\d+')
MD_RE = re.compile(r'\b([\w./-]+\.md)\b')


def tagged_lines(text):
    """Each tagged line, and the comment lines after it up to the next tag."""
    lines = text.split('\n')
    out = []
    i = 0
    while i < len(lines):
        if TAG not in lines[i]:
            i += 1
            continue
        out.append((i + 1, lines[i]))
        j = i + 1
        if re.match(r'\s*(//|#)', lines[i]):
            while j < len(lines) and re.match(r'\s*(//|#)', lines[j]) and TAG not in lines[j]:
                out.append((j + 1, lines[j]))
                j += 1
        i = j
    return out


note_scan = []
page_names = set(pages) | {DESIGN}
for n in sorted(os.listdir(DOCS)) if os.path.isdir(DOCS) else []:
    if n.endswith('.md'):
        text = read(os.path.join(DOCS, n))
        note_scan += [('docs/net-snapshot/' + n, i, l)
                      for i, l in enumerate(text.split('\n'), 1)]
for p, t in sorted(code_files.items()):
    if p in owned:
        note_scan += [(p, i, l) for i, l in enumerate(t.split('\n'), 1)]
    else:
        note_scan += [(p, i, l) for i, l in tagged_lines(t)]
spec = os.path.join(ROOT, 'protocolspec')
if os.path.isdir(spec):
    for base, _, names in os.walk(spec):
        for n in sorted(names):
            p = os.path.join(base, n)
            rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
            note_scan += [(rel, i, l) for i, l in tagged_lines(read(p))]

readme = MODULE + '/README.md'
if os.path.exists(os.path.join(ROOT, readme)):
    note_scan += [(readme, i, l)
                  for i, l in enumerate(read(os.path.join(ROOT, readme)).split('\n'), 1)]

for p, i, l in note_scan:
    if NOTE_RE.search(l):
        failures.append('%s: line %d points at a note outside the repo' % (p, i))
    # A line number moves with every edit above it; the guide names the function instead.
    ref = LINEREF_RE.search(l)
    if ref and (p.endswith(DESIGN) is False) and (p in owned or p.endswith('.md')):
        failures.append('%s: line %d cites %s by line number' % (p, i, ref.group(0)))
    # A page this comment names has to exist: a renamed page leaves citations pointing nowhere.
    if p.endswith(('.cpp', '.h', '.c', '.py', '.txt')):
        for m in MD_RE.findall(l):
            base = os.path.basename(m)
            if base in page_names or base in basenames or os.path.exists(os.path.join(ROOT, m)):
                continue
            failures.append('%s: line %d names %s, which does not exist' % (p, i, m))
        # And none of the source tree's own names, the port having renamed every one.
        if p.startswith(MODULE + '/'):
            old = OLD_NAMES.search(l)
            if old:
                failures.append("%s: line %d names the source tree's %s"
                                % (p, i, old.group(0)))

for w in warnings:
    print('warning: ' + w)
for f in failures:
    print('FAIL: ' + f)
print('doccheck: %d page(s), %d failure(s), %d warning(s)'
      % (len(pages), len(failures), len(warnings)))
sys.exit(1 if failures else 0)

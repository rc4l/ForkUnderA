#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
"""presence_diff.py - mirrors a client holds that the server holds absent.

Usage:
  python3 presence_diff.py <snappresence-sv.txt> <snappresence-clN.txt> [-v]

Both files are written into the module's own folder, M_GetFuaUserPath()/net-snapshot.

The server writes the first with fua_snap_debug_presencelog, a line a client a
dump: "sv <frame> <client> <count> <id>...", the ids that client may hold
(present, leaving, maybe, or entered in a datagram still pending). The client
writes the second with fua_csnap_debug_presencelog: "cl <frame> <player>
<count> <id>...", the ids it holds after the newest frame it applied.

For two consecutive client dumps at frames G1 < G2, an id is stuck when the
client holds it in both and no server dump for that player from G1 to G2
lists it (why that cannot misfire: docs/net-snapshot/tools.md). A pair with no server dump
inside it is skipped and counted. A file whose frames go backwards, two server
sessions in one log, is refused.

Prints the stuck count per pair with -v, then the pairs compared and skipped,
the pairs with a stuck id, the distinct stuck ids and the frames each stayed.
Exits 1 when any id is stuck.
"""

import sys


def read(path, kind):
    dumps = []
    newest = {}
    with open(path) as f:
        for n, line in enumerate(f, 1):
            parts = line.split()
            if not parts:
                continue
            if parts[0] != kind or len(parts) < 4:
                sys.exit('%s:%d: not a %s dump line' % (path, n, kind))
            frame, who, count = int(parts[1]), int(parts[2]), int(parts[3])
            if frame < newest.get(who, frame):
                sys.exit('%s:%d: frame %d after %d: dumps from two server sessions;'
                         ' delete the logs between runs' % (path, n, frame, newest[who]))
            newest[who] = frame
            ids = set(int(x) for x in parts[4:])
            if len(ids) != count:
                sys.exit('%s:%d: %d ids for a count of %d' % (path, n, len(ids), count))
            dumps.append((frame, who, ids))
    return dumps


def main(argv):
    args = [a for a in argv if a != '-v']
    verbose = len(args) != len(argv)
    if len(args) != 2:
        sys.exit(__doc__)
    sv = read(args[0], 'sv')
    cl = read(args[1], 'cl')
    if not cl:
        sys.exit('no client dumps')
    player = cl[0][1]
    if any(d[1] != player for d in cl):
        sys.exit('client dumps from more than one player')
    server = sorted((f, ids) for f, c, ids in sv if c == player)
    clientDumps = sorted((f, ids) for f, _, ids in cl)

    compared = skipped = withStuck = 0
    seen = {}
    for (g1, a), (g2, b) in zip(clientDumps, clientDumps[1:]):
        span = [ids for f, ids in server if g1 <= f <= g2]
        if g2 <= g1 or not span:
            skipped += 1
            continue
        compared += 1
        held = set().union(*span)
        stuck = (a & b) - held
        if stuck:
            withStuck += 1
        for i in stuck:
            first, last, pairs = seen.get(i, (g1, g2, 0))
            seen[i] = (min(first, g1), max(last, g2), pairs + 1)
        if verbose:
            print('frames %d-%d: %d stuck%s' % (g1, g2, len(stuck),
                  (' (' + ' '.join(str(i) for i in sorted(stuck)) + ')') if stuck else ''))

    print('player %d: %d pairs compared, %d skipped (no server dump inside)'
          % (player, compared, skipped))
    print('%d pairs with a stuck id, %d distinct ids stuck' % (withStuck, len(seen)))
    for i in sorted(seen):
        first, last, pairs = seen[i]
        print('  id %d: frames %d-%d, %d pairs' % (i, first, last, pairs))
    return 1 if seen else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

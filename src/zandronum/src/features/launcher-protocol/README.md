# features/launcher-protocol

The wire format of a launcher reply, in units that can be tested without a running server.

Two things live here so far: **reassembling a reply that arrived in pieces**, and — by reference —
the field walk that reads one, which is `features/server-browser/computation/launcherfields_compute`.
Both exist for the same reason, which is that this protocol is a stream of variable-length fields
where a single wrong byte count silently corrupts everything after it, and the corruption reads as a
plausible value rather than as an error.

## Why replies get split

A UDP datagram has a hard size ceiling and no way to grow one. Every query protocol that outgrows a
packet ends up splitting replies and numbering the pieces: Valve's A2S queries do it with a
split-packet header, Quake-derived engines do the same. Zandronum's server side already implemented
it; nothing on our side asked for it, so every reply had to fit one datagram while the field set kept
growing.

The client now asks. The opt-in is a trailing byte of exactly `2` on the query (`sv_main.cpp` reads
it), deliberately at the END rather than as a flag in the middle — an older server simply never reads
that far, so asking costs nothing against one that cannot answer.

Worth asking for even when replies fit today, because the failure when they stop fitting is a
truncated reply rather than an error.

## The format

Twelve bytes, then payload:

```
long   SERVER_LAUNCHER_CHALLENGE_SEGMENTED   (5660032)
byte   segment index, 0-based
byte   segment count
short  offset of this piece within the whole reply
short  length of this piece
short  total size of the whole reply
...    `length` bytes of payload
```

Two details that bite if missed:

- **Offsets and sizes are 16-bit**, so a reply cannot exceed 64 KB however many pieces it is cut
  into. The segment count being a byte is not the real ceiling; the offset width is.
- **A segmented reply omits the ordinary `SERVER_LAUNCHER_CHALLENGE` long** (see the
  `if ( !bSegmentedResponse )` guard on the server), so the rebuilt buffer begins directly at the
  flags long — exactly where the ordinary parser expects to be handed the stream.

Note the server segments **whenever asked**, not only when a reply is too large. So opting in means
this path runs on every query rather than rarely, which is worth knowing: it is well exercised, and a
bug in it would break the browser outright rather than only for large servers.

## Coverage is tracked per byte, not per segment

The one design decision worth defending. Counting arrivals would call a reply complete when a peer
sent segment 0 twice and segment 1 never: the count matches, the buffer has a hole, and the hole
parses as zeros — which is precisely the class of bug that produced a download port of 6400 instead
of 10777 elsewhere in this protocol.

Marking bytes makes gaps, overlaps and duplicates the same question, answered exactly. It costs one
extra byte of bookkeeping per byte of reply, bounded at 64 KB, for an in-flight assembly that
normally lasts a few milliseconds.

## Hostile by assumption

These are unauthenticated datagrams from whoever felt like sending one, so:

- a declared size is bounded **before** anything is allocated from it
- a declared piece length must actually be present in the datagram — a 4 KB piece inside a 200-byte
  packet is a lie, not a short read to patch up
- a piece must fall inside the reply it claims to belong to
- a piece from an address we never queried is dropped without allocating anything
- a duplicate is **ignored, not rewritten**: a second copy carrying different bytes is not a
  retransmission, and letting a later packet overwrite an earlier one would make the reply whatever
  arrived last

A piece whose count or total size disagrees with the assembly in progress starts a new one rather
than being refused — the ordinary cause is a second reply arriving while the first was incomplete,
which a server is entitled to do and which would otherwise wedge the assembly.

## Layout

```
computation/segmentreassembly_compute.{h,cpp}    header parsing + reassembly   (+ _test.cpp)
```

The driver is `BROWSER_ParseServerQuerySegment` in `features/server-browser/browser.cpp`, which owns
one assembly per server slot and hands the rebuilt buffer to the ordinary parser.

## Verified

18 unit tests, including out-of-order delivery, duplicates that must not overwrite, overlapping
pieces, the arrival-count trap above, sizes above 32767 (where a signed 16-bit read would go
negative), and a 40 KB reply rebuilt from 40 pieces delivered back to front.

Live: a server with `sv_maxpacketsize 60` — 48 payload bytes per segment — answers every query in
several pieces, and the browser shows every field intact and completes a join with a download through
it.

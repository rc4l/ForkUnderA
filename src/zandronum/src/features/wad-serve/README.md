# features/wad-serve

Serves this server's own WADs to the players joining it, over HTTP, so "you are missing brutal.wad"
can be answered by the machine that actually has it.

## What this is for

`features/wad-download` already fetches missing files from public mirrors, and that covers most
joins. It cannot cover the case this exists for: **a WAD that is on no mirror because it was built
ten minutes ago.** Testing a map meant uploading it somewhere between every iteration.

## Why TCP, and not the game socket

Quake 3 and Source both send file bytes in-band over the game connection. Both later grew an HTTP
escape hatch — `sv_dlURL`, `sv_downloadurl` — and that escape hatch became the thing everyone
actually uses. Odamex skipped the middle step and serves no bytes at all.

In-band means writing our own flow control, ACK and retransmit, onto a socket already carrying
ticcmds, paced by a 35 Hz loop. Over TCP the kernel does congestion control and we add a policy cap
on top: the difference between implementing a transport and implementing a rate limit.

Two consequences make it lopsided rather than merely tidier:

- **The client needed almost no new code.** It already fetches WADs over HTTP from a list of mirrors,
  verifies hashes and applies the IWAD gate. The server's endpoint is one more entry in that list, so
  `curl http://host:10666/dwango5.wad` is a valid end-to-end test with no game in the loop.
- **No amplification.** A spoofed-source UDP request could have made us fire 200 MB at a victim who
  never asked. The TCP handshake proves the peer's address before a byte moves.

The cost is real: **operators must forward TCP as well as UDP**, same port number as the game. UDP
10666 and TCP 10666 are distinct bindings to the OS, so nothing about the existing netcode changes,
but a host who only opened UDP sees downloads fail while the server itself works perfectly. Startup
says so in as many words, and `fua_downloadserver_status` exists to make it diagnosable.

## What stops it ruining the game it is attached to

The transfer threads cannot stall the main loop — separate threads, separate sockets. The contended
resource is the uplink: an uncapped transfer fills it, tic packets queue behind file bytes, and
players experience that as lag.

So the bandwidth policy is layered, and the ordering of the layers is the point:

| | Default | |
|---|---|---|
| `sv_fua_download_maxrate` | 512 KB/s | **the global budget, shared by every transfer at once** |
| `sv_fua_download_rate` | 256 KB/s | ceiling for any one transfer |
| `sv_fua_download_slots` | 4 | concurrent transfers; beyond this, 503 + `Retry-After` |
| `sv_fua_download_peraddress` | 2 | slots one address may hold |

**The global one is the limit that matters, and a per-connection cap alone is a trap** — it silently
multiplies by client count, so "256 KB/s each" becomes 5 MB/s once twenty people join at a map
change. ioquake3 gets this right with `sv_dlRate`, a server-wide budget. Source has no KB/s knob at
all and lets transfers ride the client's own `rate`, which is a large part of why every busy Source
server ends up on FastDL. UT caps size (`MaxDownloadSize`), not rate.

512 KB/s is about 4 Mbit — roughly a quarter of a modest home uplink, leaving room for the tic
stream. Twenty clients wanting a 20 MB WAD therefore take about thirteen minutes for the last one,
and that number is fixed by the global cap alone regardless of slots or per-connection rate.
Operators with real bandwidth raise that one number.

`computation/ratebucket_compute.h` holds the arithmetic, including why `BucketTakePair` exists:
charging the global budget and then discovering the connection cap is smaller spends server-wide
allowance on bytes nobody received.

## What may be served

A request names a WAD; `servepolicy_compute` matches it against the table of files the server
**already has loaded** and returns an index. No path is ever built from a remote string, so there is
no root to escape from and no symlink to follow.

`httpreq_compute` refuses to produce anything but a bare segment: `/dwango5.wad` parses,
`/wads/dwango5.wad` is rejected outright rather than normalised. A request that cannot name a
directory cannot name a parent one, so traversal stops being a check we have to get right and
becomes a shape the code will not express. Percent-decoding happens *after* that split, which is
where `%2f` gets caught trying to put a separator back.

**IWADs are refused unless they are on the shipped free list** — the same `config/iwadallowlist.txt`
names the client uses, linked from `iwadallow_compute` so there is one list and not two that drift.
A ZandroX server must not become the thing that distributes `doom2.wad`, however carelessly it was
configured.

Note the asymmetry with the client, which is deliberate: the server checks the **name**, the client
checks a **SHA-256**. The client is deciding whether to trust bytes from a stranger, so it needs the
hash. The server already knows exactly which file it opened, and an operator running a locally
modified `freedoom2.wad` is still redistributing something free — a hash check here would refuse a
legitimate file to answer a question nobody asked. The security boundary is the client's; this is a
guard rail on the operator.

## The bit that makes iterating work

Having a file by that name is not the same as having *that* file. Edit `test.wad`, restart the
server, and the name has not changed — so a client that stops at "found it" loads yesterday's bytes
and fails level authentication with a message about nothing that is actually wrong.

So `zx_joinserver.cpp` MD5s each resolved PWAD and compares it against the digest the server
published over `SQF2_PWAD_HASHES`. A mismatch drops it back out of the resolved list and refetches.
Only when the server actually sent a digest: an older server sends none, and empty has to keep
meaning "cannot compare" rather than quietly becoming "matches".

## Protocol

`SQF2_FUA_DIRECT_DOWNLOAD` (0x20): a flags byte and the TCP port. Fixed shape whichever way the
answer goes — **port 0 is how "not serving" is spelled**, rather than the field being absent, because
a field that is sometimes there desynchronises everything after it.

The address is deliberately not on the wire. The client builds the URL from the address it just
queried, so a server can say "I serve on port N" and nothing else; letting one nominate a download
host would let it point every joiner at a third party's machine.

Flags bit 0 = the operator would rather clients tried public mirrors first
(`sv_fua_download_prefermirrors`). Default off, because the case this feature exists for is a file
that is on no mirror at all — and because even when mirrors do have it, the server's copy is by
definition the one matching the MD5 it advertises.

## 503 is not "try the next mirror"

`HttpFileResult::Busy` is its own result rather than folding into `HttpError`, because 503 means the
host **has** the file and has no free slot — the ordinary state during a map change. Treating that as
"try elsewhere" would abandon the only source certain to have a file that may exist nowhere else. The
same URL is retried for up to four minutes, then the other sites get a turn.

## The one-byte bug, and the rule that comes out of it

Worth writing down, because it cost a long diagnosis and the lesson generalises.

A server's download port read as **6400** instead of 10777, deterministically, and every download
from it failed with "couldn't find it on any download site" while `curl` got 200 from that exact
endpoint at that exact moment.

The cause was a byte the browser never consumed. `SQF2_VOICECHAT` is bit 4, immediately before our
bit 5, and it writes one byte — and the browser parsed every other SQF2 field but not that one. So
every field after it was read one byte early.

The arithmetic confirms it exactly. The port goes out as 10777 = `0x2A19`, so the wire holds
`[flags 0x00][0x19][0x2A]`. Reading one byte early takes `sv_allowvoicechat`'s `1` as the flags byte
— which is why a server that never asked for it reported "prefers mirrors" — and then reads
`[0x00][0x19]` as the port, which is `0x1900` = 6400.

**The rule: a launcher client must consume every field the echoed flags carry, not merely the ones it
asked for.** Fields are variable-length, so there is no skipping an unknown one — a single unhandled
bit silently corrupts everything after it, and the corruption looks like a plausible value rather
than garbage, which is what made it survive so long. The browser now handles all six defined SQF2
bits; adding a seventh means adding its read at the same time.

Two dead ends recorded so nobody repeats them: it is **not** a size or MTU problem (dropping
`SQF_ALL_DMFLAGS`, the largest field, changed nothing), and it is **not** a buffer underflow (a guard
rejecting negative reads never fires — the bytes are real, just misaligned).

The guard stays anyway, because refusing to act on a port we did not receive is right on its own
terms. And `dumpserverlist` printing the advertised endpoint is what made the bug visible at all:
when a download fails, the first question is whether the client ever learned an endpoint.

Separately, the browser used not to opt into segmented launcher replies at all, so every reply had to
fit one datagram while the field set kept growing. That is now `features/launcher-protocol`.

## Layout

```
zx_wadserve.{h,cpp}                       listener, worker threads, CVARs, the main-thread Tick
computation/ratebucket_compute.{h,cpp}    the bandwidth policy, as arithmetic   (+ _test.cpp)
computation/httpreq_compute.{h,cpp}       the attack surface: request parsing   (+ _test.cpp)
computation/servepolicy_compute.{h,cpp}   which file may be sent, and to whom   (+ _test.cpp)
```

## Threading

The `features/updater` rule, unchanged: workers touch nothing the engine considers single-threaded.
No `Printf`, no CVARs, no `FString`, no wad tables. The servable file table and the configuration are
snapshotted on the main thread into plain types under a mutex; anything a worker wants to say goes
into a queue that `Tick()` drains. `Printf` off the main thread has crashed this engine before.

## One thing found by testing that would not have been found by reading

Refusing a client with 503 and closing gave curl a **connection reset instead of the 503**, about one
time in six. `close()` on a socket with unread received data is defined to send RST, and the peer's
TCP then discards its own receive buffer — including the reply we had just written. Every short
response here is written without having consumed the request that prompted it, so it was not a rare
race. Every path now shuts down for send, drains what the peer still had in flight, and only then
closes.

## Not done yet

**Pre-compressed serving.** UT's redirect hosts store files as `.uz` so the transfer is half the
bytes and the game host spends no CPU compressing. The same trick applies here — serve `brutal.wad.gz`
if it sits next to `brutal.wad` — but the missing half is client-side decompression, which is its own
piece of work. Deflating on the fly instead was rejected: CPU on the game host is the resource this
whole feature exists to protect.

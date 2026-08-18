---
name: server-discovery-e2e
description: Verify a ForkUnderA server is discoverable — appears on the LAN, announces to the federated registry, and carries the right country flag. Use when changing hosting, the server browser, LAN broadcast, the registry, or before a release that touches multiplayer discovery.
---

# Server discovery, end to end

Two engine instances and one assertion: does a server this machine hosts actually show up, and
does it show up the way it should — as LAN to us, and as a registry entry to everyone else.

Drive it with `fuactl`. Do not hand-roll scripts that poke the engine; if something is missing,
add it to fuactl or the bridge so the next run inherits it.

## Run it

```sh
cd tools/fuactl
node src/cli.mjs reap --kill --all              # no stragglers from earlier runs

# 1. a server. --arg passes engine arguments straight through.
node src/cli.mjs launch --iwad /path/doom2.wad --map map01 \
  --arg "-host,+sv_hostname,MY-TEST-HOST,+sv_broadcast,1" &

# 2. a client to look with
node src/cli.mjs launch --iwad /path/doom2.wad --map map01 &

# 3. ask the browser what it sees (refreshes LAN + registry, polls, prints JSON)
node src/cli.mjs browser --port <clientPort> --token <clientToken> --wait 15 \
  --expect-lan --expect-country USA
```

`--expect-lan` / `--expect-country` make it exit non-zero on failure, so it gates a release the
same way a test does. Without them it just reports.

## Reading the result

Each entry carries `lan`, `country` and `flag`. **Assert on `flag`, not `country`**: a server whose
own GeoIP lookup failed reports `XIP`, which the browser stores as an empty code and then resolves
from the address itself. `country` blank with `flag` set is normal and correct.

- **LAN** — the host appears with `lan: true` at its private address. This is the broadcast path.
- **Registry** — entries with `lan: false` came from the federated registry. Their presence proves
  the client's registry query works.
- A machine with no GeoIP database prints `GeoIP initialization failed` at startup and cannot
  resolve flags locally; check that line before believing a blank flag is a bug.

## Is the host itself reachable? Ask the host, not the browser

```sh
node src/cli.mjs hostdiag --port <hostPort> --token <hostToken> --wait 60 --expect-reachable
```

This is a different question from `browser`, and the browser genuinely cannot answer it: a host's
own public row is **fabricated** from its LAN row so the list does not flicker, and the ping on it
is a loopback. The row lights up whether or not anybody outside could reach you.

`hostdiag` reports the registry's own testimony instead — it sends an unsolicited verification from
outside, and our answering it is the proof. Fields: `reachable` (the one that matters), per-family
`families.ipv4` / `families.ipv6` each with a `state` token, and `hairpinSuspected`.

Use `--wait`: verification arrives on the 30-second announce cycle, so an unverified reading in the
first seconds means "not yet", not "broken".

## Known limit worth stating in any report

A host behind NAT on a private address (192.168.x.x) will **not** come back through the registry to
a client on the same machine, even though the LAN entry appears immediately. Announces run on a
30-second cycle and are silent on success, so absence in the list is not evidence the announce
failed.

Do not report that as a failure, and do not report it as inconclusive either — run `hostdiag`,
which answers it directly. If it says `reachable`, the server is fine and its absence from the local
browser is the host's router declining to hairpin; say that rather than "could not verify".

**IPv6 caveat as of 2026-08:** the deployed registry `registry.cantstopscrolling.net` has an A
record only. The engine resolves the registry's AAAA itself before sending the second announce, so
with no AAAA the IPv6 announce never fires and `families.ipv6` can never leave `never_announced` —
on any host, however good its v6 is. That is a DNS/droplet gap, not a code one; the registry daemon
already binds dual-stack.

## Engine surface this relies on

`browser.refresh` and `browser.list` (bridge-only RPCs, compiled out of release builds). If a
future check needs a field the list does not carry, add it there rather than parsing console text —
`dumpserverlist` prints neither the LAN flag nor the country.

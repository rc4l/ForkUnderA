# federated-server-registry

The engine's side of server discovery: announcing this server to registries, and querying them
for the server list the browser shows.

## The two things called "registry", kept apart on purpose

| term | what it is | where it lives |
|---|---|---|
| **server registry** | one deployed daemon holding a list of game servers | `src/zandronum/server-registry/` (its own executable, image `zandrox-server-registry`) |
| **federated server registry** | the engine-side logic that knows there are *many* registries — which to trust, which to announce to, how to merge their answers | **here** |

So anything named `serverregistry` talks to a single instance; anything named `federated` deals
with the set of them. `SERVER_REGISTRY_CHALLENGE` is a packet to one registry;
`FederatedServerRegistry` decides which registries exist in the first place.

## What a registry actually stores

Only addresses. Game servers heartbeat every 30s, the registry expires them after 60s, and a
launcher asking for the list gets back IP:port pairs — nothing else. Every detail the browser
shows (name, map, players, ping) comes from querying each server *directly*. That is why the
browser has a refresh ticker, and why a registry needs no database and survives a restart by
simply repopulating within a minute.

## Contents

- `sv_serverregistry.cpp` — announces this server to its registry, and answers the launcher
  queries that follow. Was `sv_master.cpp`; Zandronum-only, with no UZDoom counterpart (it
  appears zero times in the tracked upstream history), so it carries no re-sync cost here.

## Planned

The federation layer proper: a baked-in default registry list, an optional fetched list so the
defaults can be updated without a release, per-server choice of which registries to announce to,
and de-duplication of servers that appear on several. None of that exists yet.

## Build note

Per `features/README.md`, this feature's sources are listed in `add_executable( zdoom … )` in
normal source order — **before `zzautozend.cpp`**. The DObject class registry is a linker section
walked between two sentinels, so anything appended after that sentinel silently fails to register.

## config/serverregistries.txt

The list of server registries a client queries. This file is the source of truth; clients do not read
it from here. It is served through a Cloudflare Worker that caches it, and each client refreshes at
most every 6 hours — so a registry can be added by PR and reach players without shipping a release,
and GitHub never sees per-player traffic.

Line-based rather than JSON on purpose: it is edited by pull request, and a line format diffs cleanly
and cannot be broken for everyone by one missing comma. Unparseable lines are skipped rather than
failing the file, so a bad entry costs its own listing and nothing else.

**If this file moves, repoint the Worker**: `rc4l/server-registry-rc4l`,
`serverregistrylist-cdn/worker.js`, the `SOURCE` constant — it pins a `raw.githubusercontent` path on
`main`. Forgetting is quiet rather than loud: existing players keep their cached list and notice
nothing, while a *fresh* install has no cache and drops to the single registry compiled into the
engine (`kBuiltinList`). The failure lands on the population least able to report it.

A fetch that fails, times out, or returns anything that is not a list (an error page, a bot
challenge) counts as a failure: the client keeps its cached copy, and failing that the compiled-in
one. A bad fetch can never leave a player with no registries.

Being listed carries no authority — a registry hands back server *addresses* and nothing else, and
the client then asks each server directly for its name, map and players. Listing therefore does not
let a registry moderate anyone; that only happens between a server and the one registry it announces
to (`fua_serverregistry_host`).

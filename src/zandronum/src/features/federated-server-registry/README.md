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

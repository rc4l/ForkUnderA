---
name: netplay-testing
description: How to test real netplay in ForkUnderA: a locked server and clients on loopback (fuactl netplay), simulated ping and loss per client, comparing a server's world with a client's (netdiff), capturing what a server sends (netcap), and writing a hosted scenario that fails first, with observer clients where they judge something. Use for any change to netcode, the transport, unlagged, client prediction, server sends, or anything a netgame splits between machines, and whenever a report says desync, rubber-banding, missed hits or "the other player didn't see it".
---

# Testing netplay

The wire format has unit tests (`netcode-testing`). This skill is for what only a running netgame shows:
whether a client ends up holding the server's world, whether players who did not cause a change see it,
and whether unlagged puts targets where the shooter saw them. All of it runs on loopback, locally; CI
plays no game (`AGENTS.md`), so these never go in a workflow.

Run from `tools/fuactl` as `node src/cli.mjs …`, with `FUACTL_ENGINE` at the build to test.

## A netgame in one command

```bash
node src/cli.mjs netplay --players 1 --observers 1 [--map MAP01] [--iwad W] [--file a.wad] [--lag 100,0] [--loss 0,10]
node src/cli.mjs netdiff            # the server against every client of the running set; exit 1 on a difference
node src/cli.mjs netcap record --out cap --seconds 5   # what the server wrote for each client, to cap.netcap
node src/cli.mjs netcap diff a.netcap b.netcap         # commands whose counts changed, and where each client's stream parts
node src/cli.mjs netplay --quit     # stops the set by its tokens
```

- The set is a locked dedicated server (bridge `--port`, default 7820, game port 10820) and clients on the
  ports above it, all bound to `127.0.0.1` so no firewall prompt appears. Each client joins as a player and
  the command returns once the server holds every one spawned (`net.state`). It is recorded in
  `.netplay-session.json` for the other commands.
- `--lag` and `--loss` are per client, comma-separated: `fua_net_fakeping` adds that many ms to the client's
  round trip (half each way) and `fua_net_fakeloss` drops that percent of its packets each way
  (`features/net-transport`). A lone value applies to every client.
- `--for-user` leaves the first client's input unlocked for the user to play, with every other client locked.
- Ports: the user's own game is on bridge 7801; netplay uses 7820 and up and game ports 10820 and up, the
  scenarios 7840 and 10840. Quit by token (`netplay --quit`, `quit --port P`), never by process name.
- Cheats are on from launch (`sv_cheats 1`), since a later change never reaches a joined client; `god` and
  `give` typed on a client work. `setpos` and `sim.pause` are refused in a netgame: drive players with
  `+forward`, `+attack`, `input.look`, or place them with the map's starts.

## Reading a netgame

- `rpc net.state`: role, a client's connection, the server tic it last heard of and its ping, a server's
  clients with each one's slot state, ping and last confirmed tic.
- `rpc world.actors` lists synced actors by network id (carried inventory left out, `owned: 1` keeps it);
  `world.sectorstate` and `world.sidestate` page through the map's look. `sim.hash` is not comparable
  between a server and a client.
- `netdiff` compares the map once and the actors against the server's history at the tic each client last
  heard of, leaving the client's own predicted pawn's position out. Settle movers before trusting a map diff.
- `fua_unlagged_report [reset]` (server and client): each hitscan's target where unlagged rewound it on the
  server, and where the shooter's own client held it.
- `stats latency/` on a client: `packet_to_frame_ms` from a packet applied to the frame that drew it,
  `input_to_frame_ms` from input to the first frame after a tic carried it. `perfgate` and `abperf` run
  against any one instance of a set by `--port`; a server's tick is under `sim/`.
- `stats net/`: bytes and packets in and out a frame.

## A hosted scenario

`compareNetplay` (template.mjs) builds the scenario's map and, for each of its `configs`
(`{ name, players, observers, lag, loss, serverCvars, clientCvars, broadcast }`), brings up a set and hands it
to `judgeNetplay(set, label, { wadPath, config })`. The models: `netsync` (scripted changes, a kill, a late
joiner), `unlaggedhit` (rewound hits), `netbroadcast` (what an observer sees of another's actions),
`waddownload` (fetching a missing wad, two clients at once), `lanbrowse` (a LAN listing joined) and
`netmapstate` (every family only a server writes of a sector and a line). After each judge it fails an instance
whose netcode seams another thread entered (`fua_seams`) and a server that sent a map change a tic late because
no Touch marked it (`fua_net_replication`; `fua_net_replication_misses 1` prints each).

- **Joining as a player does.** `addClient(set, { files: [], via: "browser" })` launches a client without the
  set's wads and joins from its browser (`browser.add`, `browser.select`, `fua_join_selected_server`) with
  the browser on screen, since a download that finishes with it shut waits for the player to come back;
  `via: "lan"` waits for the server's LAN announce instead (`broadcast: true` in the config). Clients ask
  no public server registry.

- **Make it fail first.** `scenarios NAME --side CONFIG --cvar fua_debug_netdrop=ID` leaves a server command
  unsent (`ID` or `ID/EXT`, named by `netcap`), and `--cvar fua_debug_unlagged_ticoffset=3` makes a client
  report a server tic three behind; a list of ids is one value (`fua_debug_netdrop=151,135`). A netgame scenario that passes with the fault switched on tests nothing.
- **Hold the server to the change first.** Agreement between two worlds that never changed proves nothing,
  so check the server's own world did what the script or player meant before comparing.
- **Judge by state read at the server's tic, not by pictures**: what a client draws depends on its view and
  timing. Coarse picture checks only, if any.

## When to add an observer

| Add one | Why |
|---|---|
| A change one player causes (doors, pickups, kills, projectiles, gunfire) | a send only to the causer, or a change only predicted there, looks right everywhere but on everyone else |
| How a remote player looks (position, angle, animation) | that is what an opponent aims at |
| Hit results (pain, death, frags) | the server judges the hit; the observer proves the result reached everyone |
| Late join (`addClient(set, { role: "latejoin" })`) | a joiner must get all state |
| One client on a bad link (`--loss 0,10`) | each client must converge on its own |
| Two clients downloading from one server | wad-serve's admission only runs with concurrent clients |

Not for: the feel of a player's own movement (only the acting client and the user judge that), the wire
format (unit tests), server-only measures (tick time, ping), or single-client flows.

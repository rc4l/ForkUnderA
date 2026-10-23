# features/damage-taken — `fua_damagetaken`, the last strike you took

A console command that prints the last strike the console player took, roll by roll. A strike is every
damage roll that landed in one game tic: each pellet of a shotgun blast, a rocket's direct hit and its
splash, or an imp fireball and a shotgun blast arriving together. Each roll shows the damage before
armor, what armor absorbed, the health actually lost, who hit you and with what, the damage type, and
whether it killed you. The next strike replaces it; nothing older is kept.

```
] fua_damagetaken
Last strike: 2 hits, 15 damage (4 armor, 11 health)
  from ShotgunGuy (BulletPuff) on E2M1 at 0:42.7
  #1   10 dmg  armor   3  health   7
  #2    5 dmg  armor   1  health   4
```

Only rolls that something struck you with count: damaging floors, sector hazards, falling, drowning,
crushers and poison ticks reach `P_DamageMobj` without an inflictor and are left out, and old-style
`P_PoisonDamage` is not hooked. Hits god mode, invulnerability or an ACS damage event block cost
nothing and are not counted; a hit armor fully absorbs is.

**Network:** the server decides damage, and `SERVERCOMMANDS_DamagePlayer` only carries the new health
and armor, so the server sends each roll to the player who took it in a new extended command,
`SVC2_FUADAMAGETAKEN` (wire id 73), and to no one else. A client and server must be the same build to
connect (`DOTVERSIONSTR` and the whole revision, `GetRevisionNumber`), so no client can receive a command
it doesn't know. Client demos record the command and are tied to the exact build. There is no savegame
impact: the strike lives only in memory.

## Files

| file | role |
|------|------|
| `damagetaken.h` | Public API: `HitAmounts`, `Report`, `OnPlayerHit`, `OnServerReport`. |
| `damagetaken.cpp` | Engine glue: the strike, the labels (monster tag, player name, class names), the network-state dispatch and `CCMD( fua_damagetaken )`. |
| `computation/damagetaken_compute.{h,cpp,_test.cpp}` | Pure logic: what counts as a roll, grouping rolls into a strike, telefrag totals, labels, which actor names "what hit you", and the console text. 100% unit coverage. |
| `damagetaken_wire_nettest.cpp` | Wire tests: the pinned id, golden bytes, round trips and truncation. |

## Console command

- `fua_damagetaken` — prints the last strike, or "You haven't been struck yet."

"What hit you" is a projectile or shootable thing (a barrel) when one dealt the roll, otherwise a
player attacker's ready weapon, otherwise any other inflictor such as a monster's bullet puff, and
nothing when a monster hit directly (melee).

## Public API

- `zx::damagetaken::OnPlayerHit(player, source, inflictor, mod, hit)` — the authority's entry point.
  Offline it records the roll when `player` is the console player; on a server it sends the roll to
  that player's client (bots are skipped); in client mode it does nothing.
- `zx::damagetaken::OnServerReport(report)` — the client's entry point for a received roll.

## In-place anchors (the only edits outside this feature)

1. `p_interaction.cpp` — `P_DamageMobj`: the `fuaHit` local at the top (above every `goto`), the
   damage before armor (set again after the pre-armor damage event), the armor absorbed, the health
   before and after (after buddha), an `OnPlayerHit` in the "armor took it all" return, and one
   `OnPlayerHit` after the `SERVERCOMMANDS_DamagePlayer` send, so a client gets the health first.
2. `protocolspec/spec.misc.txt` — `Command FuaDamageTaken`, appended last.
3. `network_enums.h` — `SVC2_FUADAMAGETAKEN`, appended before `NUM_SVC2_COMMANDS`.
4. `sv_commands.h/.cpp` — `SERVERCOMMANDS_FuaDamageTaken`, sent with `SVCF_ONLYTHISCLIENT`.
5. `cl_main.cpp` — `ServerCommands::FuaDamageTaken::Execute`, ignored outside `GS_LEVEL`.
6. `CMakeLists.txt` — `features/damage-taken/damagetaken.cpp` in the zdoom source list (no
   `IMPLEMENT_CLASS`). The `*_compute.cpp` is picked up by the existing glob.
7. `tools/data/protocol-snapshot.txt` — the new command line and its wire id.

## Known trade-offs

- A spectator watching you does not get your rolls; only the player who took them does.
- A super-shotgun blast sends about 20 small commands to one client in one tic. Batching them would
  need an end-of-tic flush on the server.
- "What hit you" from a hitscan monster names its puff (`BulletPuff`), because the engine passes the puff,
  not the monster's attack, as the inflictor.
- Rolls are grouped by map name and map time, so a strike on a restarted map at the exact same tic as
  the last one would merge with it.

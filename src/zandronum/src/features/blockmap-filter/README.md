# Per-cell arrays beside the blockmap

A position check walks every actor and line in the blockmap cells its box covers. On the Sunder MAP11 laps most of
an actor's cost was memory: a linked list of 80-byte nodes, then a 1,488-byte actor read for each candidate, about
six in seven of them only for `PIT_CheckThing` to pass it by on distance.

Each cell now also keeps an array, beside its list, of what that distance test reads (x, y, radius, attack radius),
the actor and node, and whether the actor has nodes in other cells. `LinkToWorld` and `UnlinkFromWorld` keep the
arrays in step with the lists (the head of a list is the back of its array). The static lines of each cell get an
array of their boxes, built once a level.

- **Things.** `P_CheckPosition`'s walk reads the arrays: same cells, same order, the same hash of actors spanning
  cells, and skips the candidates too far to touch, which `PIT_CheckThing` returns true for at once. It holds the
  node the list's walk would go on from; when a link or unlink happens mid-walk (a ripper's kill, a pickup), the walk
  hands over to the lists there.
- **Lines.** On a map without polyobjects, the line walk skips the lines whose box misses the mover's, which
  `PIT_CheckLine` and the secnode replay both pass by. The replay now also needs the radius it was recorded with.
- **Cached values.** They must equal the live ones whenever another actor checks against them. Every move or resize
  that skips a relink refreshes them (`zx::cells::Refresh`); a check that answered from live values found none stale
  in 239 million checks over the three Sunder MAP11 laps (Complex Doom, DnD, no mod).
- **Sight.** On a map without polyobjects, `P_SightBlockLinesIterator` runs `P_SightCheckLine`'s two crossing tests
  from a second line array (32-bit ends in v1, v2 order and the line's index; a level with a line past 32 bits keeps the old walk) and marks and reads a line only when the trace crosses
  it; a line it passes by is never marked, which nothing after the check reads.
- **Swept missiles.** The quiet-step sweep reads the arrays too, and an actor's flags only when a step meets it.

`computation/cellarrays_compute.*`: the arrays' order, the line box test and the sight crossing tests; `computation/blockfilter_compute.*`: the
distance test. `cellarrays.*`: the arrays, the refresh and the line walk.

Hooks: `p_maputl.cpp` (`LinkToWorld`, `UnlinkFromWorld`, `FBlockThingsIterator`), `p_local.h` (the iterator's
walk), `p_map.cpp` (`P_CheckPosition`, `P_CreateSecNodeList`'s replay), `p_setup.cpp` (arrays with the blockmap
lists, the line arrays built after `PO_Init`), `p_sight.cpp` (`P_SightBlockLinesIterator`). Refreshes after `BeginPlay` (`p_mobj.cpp`, `g_game.cpp`, `cl_main.cpp`), the crush and gib radius,
`P_CheckMissileSpawn`'s advance, Boom friction and the Vile's raise (`p_enemy.cpp`), `P_Thing_Raise`
(`p_things.cpp`), the resurrect cheat (`m_cheat.cpp`), `A_CustomMissile`'s and `A_CustomRailgun`'s aim offsets
(`thingdef_codeptr.cpp`), the bridge orbit (`a_bridge.cpp`), the Revenant tracer (`a_revenant.cpp`), the Strife
grenade (`a_strifeweapons.cpp`), the Wraith effects (`a_wraith.cpp`) and swept fast projectiles.

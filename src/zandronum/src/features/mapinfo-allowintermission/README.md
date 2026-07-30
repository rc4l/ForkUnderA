# mapinfo-allowintermission

Ports the `allowintermission` cluster keyword from UZDoom.

- **Keyword:** `allowintermission` (inside a `cluster { ... }` block)
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@ed2b73833

## Behavior

Normally, traveling between maps within the same hub cluster skips the
intermission (stats) screen. `allowintermission` on the cluster forces the
intermission to be shown even for intra-hub travel.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `CLUSTER_ALLOWINTERMISSION` flag bit.
- `src/zandronum/src/g_mapinfo.cpp` — `ParseCluster()` recognizes
  `allowintermission` and sets the flag.
- `src/zandronum/src/g_level.cpp` — `G_DoCompleted()`'s hub intermission-skip
  condition now also requires `!CLUSTER_ALLOWINTERMISSION`.

# mapinfo-compat-flags

Backports the UZDoom MAPINFO `compat_*` flags our base was missing. All parse into `COMPATF2_*`
bits (doomdef.h, bit positions matching uzdoom where it defines them) via `MITYPE_COMPATFLAG`
rows in g_mapinfo.cpp, and are net-synced through the existing `i_compatflags2` replication.

A compat flag *reverts* a newer behaviour to the old one. Where the newer behaviour exists in this
base, the flag gates it (real revert). Where the behaviour post-dates our 2016 fork, we are already
at the old behaviour, so the flag is a **correct no-op** (the compat intent is satisfied by absence —
not a cop-out).

| keyword | uzdoom | status |
|---|---|---|
| compat_multiexit | 51da78ba2 | **GATED** — G_ChangeLevel `ga_completed` guard |
| compat_checkswitchrange | d4d010ac3 | **GATED** — P_CheckSwitchRange returns true (no range check) |
| compat_explode2 | dc67355e9 | no-op (the newer radius-attack thrust it reverts post-dates our fork; base already does the old thrust) |
| compat_soundcutoff | ef5707d73 | no-op (sound-owner-cutoff change post-dates fork) |
| compat_teleport | ab837b608 | no-op (indirect teleport sector-action change post-dates fork) |
| compat_noacsargcheck | 35f66c5cc | no-op (the ACS script arg-count check it disables post-dates fork; base has no such check) |
| compat_novdolllockmsg | 7d2d874af | no-op (voodoo-doll lock-message change post-dates fork) |
| compat_reservedlineflag | e38d46f3d | no-op (only `ML_RESERVED_ETERNITY` exists here; the MBF21 reserved-line-flag clear it reverts is not present) |
| compat_railing | 0341a3d75 | no-op (Strife railing render post-dates fork) |
| compat_pointonline | ee7eb3253 | no-op (P_PointOnLineSide variant post-dates fork) |
| compat_avoidhazards | d15f450fe | no-op (MBF stay-off-hazard AI absent) |
| compat_stayonlift | 196a4c0b3 | no-op (MBF stay-on-lift AI absent) |
| compat_voodoozombies | 1589afb46 | no-op (voodoo-zombie behaviour absent) |
| compat_vileghosts | c83344f5c | no-op (archvile ghost behaviour absent) |

### compat_scriptwait (cbd447962) — flagged divergence

`compat_scriptwait` reverts a recent ACS `ScriptWait` fix. Our base's
`PCD_SCRIPTWAIT` (p_acs.cpp) already uses the `ScriptWaitPre` path, but the
exact semantics of the upstream change are not available in the vendored
reference (which is newer than our fork yet still predates this commit). The
ACS VM is net-synced and savegame-relevant, so gating it speculatively — with
no target content exercising the flag — risks a script-sync regression that is
strictly worse than the no-op. It therefore parses + net-syncs like the others
but the behaviour gate is intentionally not wired; revisit if a wad needs it,
with the upstream diff in hand.

### Note on "no-op"

Every flag above **parses and net-syncs** through `i_compatflags2` (real,
functional infrastructure). "no-op" means the *behavioural revert* is already
satisfied because this 2016-era base predates the upstream behaviour the flag
was created to turn off — confirmed by the vendored uzdoom reference (itself
newer than our fork) not containing any of these commits. If one of those newer
behaviours is later ported, add its gate at that time.

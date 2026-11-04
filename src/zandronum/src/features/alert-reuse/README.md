# Repeated noise alerts (`sv_fua_alertreuse`)

A weapon that alerts monsters on every shot floods the same sectors again every few tics. On a large
connected map that is the whole map each time: on Sunder MAP11 with Complex Doom, ~3,300 sectors and
~17,500 actors per alert, for monsters that heard the first one.

`P_NoiseAlert` now skips an alert when the same target (and splash) ran a full one at most
`sv_fua_alertreuse` tics ago (default 8, `0` runs every alert) and the emitter is inside the area
that one reached without crossing a sound-blocking line. Only a full alert starts the window again,
so a monster that walks into the area, or spawns there, during the window learns of the target at
most that many tics later. Alerts with a maximum distance always run in full.

- `computation/alertreuse_compute.*`: the memo of each target's last full alert and the decision.
- Hooks: `p_setup.cpp` `P_SetupLevel` (`P_ForgetNoiseAlerts`, a new level starts with no memos), `p_enemy.cpp` `P_NoiseAlert` (decide, start the memo) and `NoiseMarkSector` (mark sectors
  reached unblocked).

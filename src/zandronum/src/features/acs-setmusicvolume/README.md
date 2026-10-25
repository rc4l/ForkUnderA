# ACS `SetMusicVolume`

Ports UZDoom's `SetMusicVolume(fixed volume)` ACS function (`uzdoom@a1e8d83724`), with the level state
it later gained (`uzdoom@da74b9df95`).

```
SetMusicVolume(0.5);   // half the song's volume
SetMusicVolume(1.0);   // back to normal
```

- It is ACS function **97**, upstream's number, which ACC compiles `SetMusicVolume` to (`-97` in
  zspecial.acs). It sits out of order in `EACSFunctions`, as `Warp = 92` does.
- The volume is a factor on the song's own volume (`$musicvolume` in SNDINFO), held to 0..2 by
  `I_SetMusicVolume`. `snd_musicvolume` still applies on top.
- It lasts until the level ends: a new level starts at 1, a savegame keeps it, and a Strife dialogue,
  which lowers the music while a voice plays, restores it rather than 1.
- As upstream, a new song (ACS `SetMusic`, MUSINFO) starts at its own volume and drops the factor until
  the next `SetMusicVolume`, though the level keeps the value for a save.

## Online

Only clients play music, so a server's script sends `SVC2_FUASETMUSICVOLUME` (a float) to every client.
The server also keeps the factor the clients play the current song at, which a new song drops as it does
for them, and a joining client gets that right after the map music, whose start would otherwise reset it. `fuactl scenarios netmusic`
holds the player and a client joining after each change to the same song and volume. A client
holds what arrives to 0..2 and plays a value that is not a number at 1 (`ComputeNetMusicVolume`). The
command is appended after the last existing one, so no id moves; `tools/data/protocol-snapshot.txt`
records it.

## Savegames

`SAVEVER` 4525: `P_SerializeSounds` stores the factor after the music name and order. A save from 4524
or earlier loads at 1. Upstream's keyed saves needed no bump for the same change.

## In-engine hooks

- `p_acs.cpp`: `ACSF_SetMusicVolume = 97` and its case; its send raised `p_acs.cpp`'s
  `net-sends-through-replication` allowance by one (`tools/boundary-allow.json`).
- `g_level.h` / `g_level.cpp`: `FLevelLocals::MusicVolume` and `SetMusicVolume`, 1 in
  `G_InitLevelLocals`, reset in `G_ChangeLevel`.
- `p_setup.cpp`: a fresh level applies its volume; `p_saveg.cpp`: the save field; `version.h`: `SAVEVER`.
- `p_conversation.cpp`: both dialogue exits restore the level's volume.
- `protocolspec/spec.misc.txt`, `network_enums.h`, `sv_commands.{h,cpp}`, `cl_main.cpp`, `sv_main.cpp`
  (the two join-time music sends): the command.
- `sv_main.{h,cpp}`: `SERVER_SetMapMusicVolume`, set by the ACS case and a loaded save and reset by
  `SERVER_SetMapMusic`, which every song change on a server calls.

The pure part is `computation/musicvolume_compute`; `musicvolume_wire_nettest.cpp` pins the command's
bytes.

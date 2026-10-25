# Music cache

A song stored compressed in a pk3 is inflated whole before it plays, and a big one takes a frame or
several to inflate: 90–110 ms for a 22 MB song, 23–34 ms for a 5.6 MB one. Before this, every music
change inflated it again, so an ACS `SetMusic` mid-level stalled the game. Now the level's songs are
inflated once, as it loads, and kept until a later level no longer names them.

- **What is kept:** the level's MAPINFO song, then every string of the level's ACS modules (the map's
  BEHAVIOR and the loaded libraries) that names a song, as S_ChangeMusic would look it up (LANGUAGE's
  `$` names, SNDINFO's `$musicalias`, `file://`). MUSINFO's songs are not kept.
- **What is not:** a song stored uncompressed (in a WAD, or a pk3 entry stored without compression)
  opens in a few milliseconds either way, so it is never inflated or kept.
- **How much:** `snd_fua_musiccache_mb` (16–256, default 128), filled in the order above; a song too big
  for what is left is passed over for smaller ones after it. A new size takes effect at the next level.
- **Off:** `snd_fua_musiccache 0` lets every kept song go at once and keeps none; a music change then
  inflates as before. Nothing is kept on a server, with music off (`-nomusic`) or at music volume 0.
- A playing song holds its own bytes, so one dropped from the cache plays on, and the precache keeps
  the song the level already started without inflating it again.

Both settings are under Options → Sound options, as "Precache level music" and "Level music cache
(MB)". `fua_musiccache` lists the songs kept for the level, the memory they take, and how many music
changes since its precache played bytes already in memory or inflated a song. `fuactl scenarios
netmusic`'s pk3 side holds a netgame's clients to keeping the songs and inflating none.

## In-engine hooks

- `s_sound.cpp` `S_ChangeMusic`: a compressed song's bytes come from `zx::musiccache::Load` and play
  through `zx::musiccache::Reader`, which replaced the one global buffer each change overwrote.
- `p_setup.cpp` `P_SetupLevel`: `zx::musiccache::PrecacheLevel` beside the textures' and sounds'
  precache, queued for the handoff when a tic on its own thread loads the level, since reading lumps is
  the main thread's.
- `s_advsound.cpp` `S_ClearSoundData`: `zx::musiccache::Clear`, before a reload renumbers the lumps.
- `wadsrc/static/menudef.txt`: the two rows in `SoundOptions`.
- `features/telemetry`: `audio/music_read`, `load/music_precache` and `audio/music_cache_bytes`.

The pure part is `computation/musiccache_compute`: the budget, which songs fit it, and the name a
song's lump is found by.

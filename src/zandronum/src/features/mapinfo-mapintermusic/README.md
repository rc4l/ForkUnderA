# mapinfo-mapintermusic

Ports the `mapintermusic` MAPINFO map keyword from UZDoom.

- **Keyword:** `mapintermusic = "<targetmap>", "<music>"[, <order>]`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@bb7e19120

## Behavior

Chooses the intermission music based on which map is being entered next.
When the current level exits toward `<targetmap>`, the intermission plays
`<music>` (with optional track order) instead of the level's default
`intermusic`/gameinfo intermission music. Multiple entries may be declared,
one per possible destination.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `FInterMusicEntry { FString music; int order; }`,
  `FInterMusicMap` (`TMap<FName, FInterMusicEntry>`), and
  `level_info_t::MapInterMusic`.
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — `MapInterMusic.Clear()`.
  - `DEFINE_MAP_OPTION(mapintermusic)` — parses `targetmap, music[, order]`.
- `src/zandronum/src/wi_stuff.cpp` — the intermission music selection checks
  `MapInterMusic.CheckKey(FName(wbs->next))` before falling back to
  `InterMusic` / the gameinfo default.

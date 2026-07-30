# mapinfo-author

Ports the `author` MAPINFO map keyword from UZDoom.

- **Keyword:** `author = "<name>"`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@3e9921696

## Behavior

Stores the map's author string and displays it, centered in the small font,
directly below the level name on the intermission "Finished" summary screen —
matching UZDoom's placement.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::AuthorName` (`FString`).
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — clears `AuthorName`.
  - `DEFINE_MAP_OPTION(author)` — parse handler.
- `src/zandronum/src/wi_stuff.cpp`
  - `authortext` static, set from `level.info->AuthorName` in `WI_Start`.
  - `WI_drawLF()` draws it below the level name (`SmallFont`, `CR_GREY`).

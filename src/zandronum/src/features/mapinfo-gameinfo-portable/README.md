# mapinfo-gameinfo-portable

Real-behavior ports of GAMEINFO keys from UZDoom that our base can honor.

| Keyword | uzdoom sha | Behavior |
|---|---|---|
| `normforwardmove` | a1cc548af | Sets the walk/run forward-move speed defaults. |
| `normsidemove` | a1cc548af | Sets the walk/run side-move (strafe) speed defaults. |
| `hidepartimes` | a2f8b7d0d | Suppresses par-time display on the intermission screen. |
| `dontcrunchcorpses` | a1cc548af | Crushers leave corpses intact instead of gibbing them. |

## Code hooks (in-place)

- `src/zandronum/src/gi.cpp`
  - `extern float normforwardmove[2], normsidemove[2];` (globals in g_game.cpp).
  - `ParseGameInfo()` custom `normforwardmove` / `normsidemove` blocks (two ints each).
  - `GAMEINFOKEY_BOOL(hidepartimes)`, `GAMEINFOKEY_BOOL(dontcrunchcorpses)`.
- `src/zandronum/src/gi.h` — `gameinfo_t::hidepartimes`, `dontcrunchcorpses`.
- `src/zandronum/src/wi_stuff.cpp` — par display gated on `!gameinfo.hidepartimes`.
- `src/zandronum/src/p_mobj.cpp` — `AActor::Grind()` corpse-gib branch gated
  on `!gameinfo.dontcrunchcorpses`.

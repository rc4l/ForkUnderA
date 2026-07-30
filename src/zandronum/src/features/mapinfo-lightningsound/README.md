# mapinfo-lightningsound

Ports the `lightningsound` MAPINFO map keyword from UZDoom.

- **Keyword:** `lightningsound = "<sound>"`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@ce2a0c929

## Behavior

On levels with lightning enabled, each flash plays a thunder sound. The base
engine hardcoded `"world/thunder"`; this keyword overrides it per level.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::LightningSound` (`FString`).
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — clears `LightningSound`.
  - `DEFINE_MAP_OPTION(lightningsound)` — parse handler.
- `src/zandronum/src/g_shared/a_lightning.cpp` —
  `DLightningThinker::LightningFlash()` plays
  `level.info->LightningSound` when set, else `"world/thunder"`.

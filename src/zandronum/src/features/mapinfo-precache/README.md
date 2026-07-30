# mapinfo-precache

Ports the `PrecacheClasses` and `PrecacheTextures` MAPINFO map keywords from
UZDoom. (The base engine already had `PrecacheSounds`.)

- **Keywords:** `PrecacheClasses = "<class>"[, ...]`, `PrecacheTextures = "<tex>"[, ...]`
- **Class:** PORTABLE (real behavior)
- **Upstream:** PrecacheClasses uzdoom@65e158954, PrecacheTextures uzdoom@3849cb862

## Behavior

Both feed the level texture-precache hitlist so the named assets are cached
at level load rather than hitching on first use:

- `PrecacheTextures` — each named texture's index is set in the hitlist.
- `PrecacheClasses` — for each named actor class, every one of its states'
  sprites is added to the sprite list, so the class's graphics precache even
  if no instance is currently spawned (mirrors the live-actor seeding already
  in `GetHitlist`).

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::PrecacheClasses`
  (`TArray<FName>`) and `PrecacheTextures` (`TArray<int>`, raw texture indices
  to avoid pulling `textures.h` into this header).
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — clears both.
  - `DEFINE_MAP_OPTION(PrecacheClasses/PrecacheTextures)` parse handlers
    (textures resolved via `TexMan.CheckForTexture`, stored as index).
- `src/zandronum/src/v_video.cpp` — `DFrameBuffer::GetHitlist()` seeds the
  sprite list from each PrecacheClass's `FActorInfo::OwnedStates`.
- `src/zandronum/src/textures/texturemanager.cpp` —
  `FTextureManager::PrecacheLevel()` marks the PrecacheTextures indices.

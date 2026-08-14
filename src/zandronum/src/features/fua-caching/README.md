# fua-caching — level-load asset precache (`cl_fua_caching`)

Stock behavior loads every texture and most sounds lazily on first use — `gl_precache`
defaults off, and even when on, the hitlist only covers each placed actor's *current*
sprite. In effect-heavy mods a mass death streams hundreds of never-seen explosion
sprites and sounds mid-fight, producing multi-hundred-ms hitch frames (measured: Scythe2
MAP30 + Complex Doom `kill monsters`, worst frame 482 ms cold vs 211 ms warm).

`cl_fua_caching` (Int, default 2, archived; menu: FUA Options → Asset caching):

- **0 — Off**: stock lazy loading, byte-identical code paths.
- **1 — Placed actors**: every state of every placed actor class (and its ancestors).
- **2 — Full (spawn closure)**: mode 1 plus the transitive closure over constant
  DECORATE state parameters (`A_SpawnItemEx("Foo")` etc. are constant-folded to
  `FxConstant` at parse time — read without running the evaluator), drop-item chains,
  actor replacements, and blood types. Run-time-computed spawn classes stay lazy.
  Closure capped at 4096 classes (excess reported, stays lazy).

Sound side mirrors it: property sounds via `MarkPrecacheSounds()` on each closure
class's Defaults, plus constant sound arguments from state parameters.

Client-only by construction: callers (`FTextureManager::PrecacheLevel`,
`S_PrecacheLevel`) already skip dedicated servers and demo playback, and nothing here
touches sim state. A summary line (`FUA caching: N textures (X MB) precached in T ms`)
prints whenever the mode is active.

## In-place engine edits

- `thingdef/thingdef.h`, `thingdef/thingdef_expression.cpp` — `FStateExpressions::GetOwner()`
  accessor (the flat StateParams table stores the owning class; the closure groups
  constant refs by it).
- `v_video.cpp` (`DFrameBuffer::GetHitlist`) — calls `FUA_MarkCachedSprites()`; sprite
  hitlist entries stamped `8` (`HIT_Sprite`) in modes 1–2 so the GL precacher builds
  the *expanded* material sprites actually render with (stock `5` built the wall
  variant — the GL sprite-precache branch was dead code).
- `textures/texturemanager.cpp` (`PrecacheLevel`) — summary Printf.
- `gl/textures/gl_texture.cpp` (`FTexture::PrecacheGL`) — upload gate is
  `gl_precache || FUA_CachingMode() > 0` (gl_precache kept as legacy opt-in).
- `gl/scene/gl_scene.cpp` (`FGLInterface::PrecacheTexture`) — in modes 1–2, sprite
  materials are exempt from the per-level `UncacheGL` eviction so warm effect sprites
  survive map changes; level geometry still evicts as before.
- `s_sound.cpp` (`S_PrecacheLevel`) — calls `FUA_MarkCachedSounds()` before the
  cache/unload loops (the unload loop actively evicts unmarked sounds, so marking must
  come first); sound-count Printf.
- `wadsrc/static/menudef.txt` — `FuaCachingModes` OptionValue + FUA Options row.
- `CMakeLists.txt` — `fua_caching.cpp` registered before `zzautozend.cpp`.

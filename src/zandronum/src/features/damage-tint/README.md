# features/damage-tint — damaging floors color the player

Overwatch-style status readability: a player standing on a damaging floor picks up the floor's own
color — an **emissive gradient** climbing from the feet (full tint) to about the waist (gone) on
body sprites, and a faint flat tint on the first-person weapon. The color is the floor texture's
average, so any mapper's custom nukage in any WAD produces the right tint automatically; no curated
tables. Ramps in over ~8 tics, lingers ~16 after stepping out, and throbs in sync with the 32-tic
damage cycle.

Client-side visual only: derived entirely from replicated state (position + sector), no sim
contact, no net traffic, nothing observable by savegames, demos, netcode, or ACS. Radsuit
suppresses it exactly like it suppresses the damage. Render styles that own their color (stencil,
shaded, fuzz, subtractive) are never touched — the hooks only act on `STYLEOP_Add` sprites whose
color is still pure white.

The gradient rides the renderer's existing glow-plane shader path (`EnableGlow`/`SetGlowParams`
against the actor's own floor plane) — per-pixel falloff on the GPU, no new shader code.

## Files

| file | role |
|------|------|
| `damagetint.h/.cpp` | Engine glue: per-actor intensity ramps, per-texture average-color cache, the render hooks. The dedicated server (`SERVER_ONLY`) gets inert stubs; a build without GL keeps the cvars and tints through the Vulkan backend. |
| `features/gamecopy/tintsubject.h/.cpp` | The game half, read between tics into the render copy: the sector damage table and whether an actor's floor hurts it unprotected (`TintSubjectOf`); `fua_tintdebug` stays in `damagetint.cpp`. |
| `computation/damagetint_compute.{h,cpp,_test.cpp}` | Pure math: ramp step, damage-cycle pulse, effective strength, white→floor channel blend. 100% unit coverage. |

## Cvars

- `cl_damagetint` (0–100, default 35) — body-sprite gradient strength; 0 disables.
- `cl_damagetint_weapon` (0–100, default 18) — first-person weapon tint strength.

## In-place anchors (the only edits outside this feature)

1. `gl/scene/gl_sprite.cpp` — include + `DamageTint_BeginSpriteGlow(...)` right after the
   `SetObjectColor(ThingColor)` in `GLSprite::Draw`, and the paired `DamageTint_EndSpriteGlow()`
   in the state-restore block at the end of `Draw`.
2. `gl/scene/gl_weapon.cpp` — include + `DamageTint_WeaponGradient(...)` before the psprite's
   `Apply`, setting GL's tint and the weapon record's `tint`.
3. `g_shared/shared_sbar.cpp` — `DamageTint_Arm2D` / `DamageTint_Disarm2D` around the face's
   `DrawTexture`.
4. The Vulkan backend reads the same gradients: `features/sprites/spritebuild.cpp`
   (`DamageTint_SpriteGradient` per band into `MeshPiece::tint`), `features/sprites/pspritebuild.cpp`
   (`DamageTint_WeaponGradient` into `Quad2D::tint`) and `features/draw2d/record2d.cpp`
   (`DamageTint_Armed2D` for the face); the shaders' `fuaDamageTint` is `ComputeTintTexel`.
5. `CMakeLists.txt` — `features/damage-tint/damagetint.cpp` and `features/gamecopy/tintsubject.cpp` in the zdoom source list (no
   IMPLEMENT_CLASS, so ordering is irrelevant; kept with the other features).

## Known trade-offs

- The per-actor ramp map is keyed on the actor pointer; an address reused by a freshly spawned
  actor can inherit at most one frame of a stale fade-out. Cosmetic, self-correcting.
- `averageColor()` expects GL-order RGBA but `FBitmap` stores BGRA — the glue swaps r/b on the way
  out (same gotcha the sky-tint trial hit).
- GL and Vulkan; the software renderer is untouched.

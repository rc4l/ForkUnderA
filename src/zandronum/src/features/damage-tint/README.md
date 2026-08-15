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
| `damagetint.h/.cpp` | Engine glue: eligibility (players, damaging floor, no radsuit), per-actor intensity ramps, per-texture average-color cache, the two render hooks. `NO_GL` builds get inert stubs. |
| `computation/damagetint_compute.{h,cpp,_test.cpp}` | Pure math: ramp step, damage-cycle pulse, effective strength, white→floor channel blend. 100% unit coverage. |

## Cvars

- `cl_damagetint` (0–100, default 35) — body-sprite gradient strength; 0 disables.
- `cl_damagetint_weapon` (0–100, default 18) — first-person weapon tint strength.

## In-place anchors (the only edits outside this feature)

1. `gl/scene/gl_sprite.cpp` — include + `DamageTint_BeginSpriteGlow(...)` right after the
   `SetObjectColor(ThingColor)` in `GLSprite::Draw`, and the paired `DamageTint_EndSpriteGlow()`
   in the state-restore block at the end of `Draw`.
2. `gl/scene/gl_weapon.cpp` — include + `DamageTint_ModulateWeapon(ThingColor, ...)` right after
   the psprite's ThingColor is computed.
3. `CMakeLists.txt` — `features/damage-tint/damagetint.cpp` in the zdoom source list (no
   IMPLEMENT_CLASS, so ordering is irrelevant; kept with the other features).

## Known trade-offs

- The per-actor ramp map is keyed on the actor pointer; an address reused by a freshly spawned
  actor can inherit at most one frame of a stale fade-out. Cosmetic, self-correcting.
- `averageColor()` expects GL-order RGBA but `FBitmap` stores BGRA — the glue swaps r/b on the way
  out (same gotcha the sky-tint trial hit).
- GL renderer only; the software renderer is untouched.

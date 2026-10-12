# features/sprites

The Vulkan backend's sprites, derived from the actors instead of taken from GL's `GLSprite`.
`spritebuild.cpp` does what `GLSprite::Process`, `GLSprite::Draw` and `RegisterSprite` do for one
actor: visibility, interpolation, the rotation the viewer sees, the trimmed frame and its scale, the
floor and ceiling clip, light (glow, colormap, 3D-floor light bands, the actor's dynamic light),
render style, and the quad, billboarded or rolled. The decisions are `Compute*` units in
`computation/`; the glue reads the engine and appends to the level mesh's dynamic stream.

Particles too (`BuildParticle`, `GLSprite::ProcessParticle`'s light and quad), each walk handing over
the particles in the subsectors it reaches. A sprite's `ThingColor` (a particle's colour, a stencil
actor's fill), which GL's shader multiplies it by, is folded into its shading on both this path and
GL's capture; without it every particle drew in its texture's white (scenario `particlecolor`).

Models still go through GL (`ProcessSprite`, then `gl_RenderModel` and `CaptureModelSurface`), so a
stacked view in standalone, whose actors `features/visibility` hands here from inside that view, draws
no models yet.

`fua_sprite_check 1` derives every sprite beside GL's and compares the two bit for bit;
`fua_sprite_report` prints what differed. It matched on about 1.6 million actor-frames across
AOW03, wdiex02, WDI01, WDI09, WDI18 and dbab04, with XY billboards, `gl_spriteclip 2` and `3`,
`gl_sprite_blend`, `gl_fuzztype`, dynamic sprite lights, invulnerability and light amplification.
`fua_sprites_own 0` hands every sprite back to GL.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`, the standalone sprite sweep: `BuildSprite` per
  actor in view, `ProcessSprite` for a model or with the switch off, and `CheckFrameSprites` after
  GL's sprites are drawn; `StackViewSprite`, `BuildSprite` for each actor a stacked view's walk reaches;
  `WalkParticles`, `BuildParticle` for each particle the player's walk reaches.
- `features/levelmesh/flatmesh.cpp` `RegisterSprite`: `NoteGLSprite`, GL's piece for the check, and the
  `ThingColor` fold.

GL settings it still reads, until the renderer interface supplies them (P5): `glset`,
`gl_fixedcolormap`, `currentmapsection`, `getExtraLight`, `gl_FindModelFrame`, and GL's sprite cvars.

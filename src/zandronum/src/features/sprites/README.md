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

Models too (`modelbuild.cpp`): the actor's light and style as a single-band sprite's, the pose from
`computation/modelpose_compute` (`ComputeModelMatrix`, `gl_RenderModel`'s VSMatrix calls; the momentum,
inherited, spin, align and roll-against angles; `ComputeModelInter`, the frame blend), and each model's
surfaces from `FModel::FrameSurfaces`, captured through `CaptureModelSurface`. GL's own model path uses
the same pose and blend, so the two cannot drift. Standalone drew no solid models before this, since only
`RenderScene` draws GL's model list, and none in stacked views or mirrors (scenario `modelstandalone`).

`fua_sprite_check 1` derives every sprite and model beside GL's and compares the two bit for bit, every
vertex of a model surface included, matching an actor's pieces within one walk; `fua_sprite_report`
prints what differed and how many model surfaces were compared. It matched on about 1.6 million
actor-frames across AOW03, wdiex02, WDI01, WDI09, WDI18 and dbab04, with XY billboards, `gl_spriteclip 2`
and `3`, `gl_sprite_blend`, `gl_fuzztype`, dynamic sprite lights, invulnerability and light amplification;
and with models on about 4.2 million more actor-frames and 103,000 model surfaces across the Whodunit
maps. In standalone the actors only a mirror or stacked view sees count as "only ours", since GL walks
neither there. `fua_sprites_own 0` hands every sprite and model back to GL.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`, the standalone sprite sweep: `BuildSprite` per
  actor in view, `ProcessSprite` with the switch off or the check on, GL's model list with the check on,
  and `CheckFrameSprites` after GL's sprites are drawn; `StackViewSprite`, `BuildSprite` for each actor a
  stacked view's or mirror's walk reaches; `WalkParticles`, `BuildParticle` for each particle the player's
  walk reaches.
- `features/levelmesh/flatmesh.cpp` `RegisterSprite`: `NoteGLSprite`, GL's piece for the check, and the
  `ThingColor` fold.
- `features/levelmesh/modelmesh.cpp`: `ModelPieceLook`, a model's look for GL's capture and ours;
  `CaptureModelSurface` notes GL's surfaces for the check (`NoteGLSprite`) and holds a checked derived
  model's (`NoteDerivedSprite`).
- `gl/models/gl_models.cpp` `gl_RenderModel` and `gl_RenderFrameModels`: `ModelPoseFor` with
  `ComputeModelMatrix`, and `NextModelFrame`.
- `gl/models/gl_models_md2.cpp`, `gl_models_md3.cpp`, `gl_voxels.cpp`: `FrameSurfaces`, each `RenderFrame`'s
  surfaces worked out without GL and drawn from.

GL settings it still reads, until the renderer interface supplies them (P5): `glset`,
`gl_fixedcolormap`, `currentmapsection`, `getExtraLight`, `gl_FindModelFrame`, and GL's sprite cvars.
Model data is still GL's: the loaders in `gl/models`, and the vertices `CaptureModelSurface` reads from the
CPU copy `FModelVertexBuffer` keeps beside its GL buffer.

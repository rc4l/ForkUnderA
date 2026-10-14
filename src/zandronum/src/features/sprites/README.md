# features/sprites

The Vulkan backend's sprites, derived from the actors instead of taken from GL's `GLSprite`.
`spritebuild.cpp` does what `GLSprite::Process`, `GLSprite::Draw` and `RegisterSprite` do for one
actor: visibility, interpolation, the rotation the viewer sees, the trimmed frame and its scale, the
floor and ceiling clip, light (glow, colormap, 3D-floor light bands, the actor's dynamic light),
render style, and the quad, billboarded or rolled. The decisions are `Compute*` units in
`computation/`; the glue reads the engine and appends to the level mesh's dynamic stream.

Particles too (`BuildParticle`, `GLSprite::ProcessParticle`'s light and quad), each walk handing over
the particles in the subsectors it reaches. A sprite's `ThingColor` (a particle's colour, a stencil
actor's fill), which GL's shader multiplies it by, is folded into its shading here and in the copy of GL's
sprite the check reads; without it every particle drew in its texture's white (scenario `particlecolor`).
Night vision's inverted monsters and items take GL's `TM_INVERSE` (`ComputeTextureMode`, scenario `lightamp`), and
`r_usecolorblending` blends an opaque fullbright additive sprite by its own colour (`ComputeColorBlending`,
scenario `colorblend`), as `gl_GetRenderStyle` does.

The rest of `GLSprite::Draw`'s translucent setup is `ComputeSpriteDrawLook`, which every sprite, model and
GL's copy for the check shade through (`CaptureSpriteShading`): a fuzzy actor under `r_fuzztype 0` is GL's
shadow, grey 0.2 at alpha 0.44 thinned by coloured fog over its distance, and additive and shadow styles fog
toward black. Every op `gl_GetRenderStyle` has no equation for (the shadow, a fuzzy decal's raw fuzz) blends
with its `DST_COLOR` / `ONE_MINUS_SRC_ALPHA` fallback, the projected decals' fuzz pass included. Before this a
spectre drew solid at the default fuzz type. A model whose actor is not in exactly `STYLE_Normal` loses the
faces `gl_RenderModel` culls (clockwise front faces, counter-clockwise on the HUD; `ComputeModelCull`, tested
against `gl_FrontFacing` in the shader), and a blended HUD model writes depth as GL's HUD step does. GL does
subtract a subtract-style model: `Draw` turns its style to fuzz only after setting the subtract blend.
Scenarios `shadowstyles` and `hudmodelcull`.

Models too (`modelbuild.cpp`): the actor's light and style as a single-band sprite's, the pose from
`computation/modelpose_compute` (`ComputeModelMatrix`, `gl_RenderModel`'s VSMatrix calls; the momentum,
inherited, spin, align and roll-against angles; `ComputeModelInter`, the frame blend), and each model's
surfaces from `FModel::FrameSurfaces`, captured through `CaptureModelSurface`. GL's own model path uses
the same pose and blend, so the two cannot drift. Standalone drew no solid models before this, since only
`RenderScene` draws GL's model list, and none in stacked views or mirrors (scenario `modelstandalone`).

The weapon too (`pspritebuild.cpp`, `computation/pspritequad_compute`): each layer below the targeter as
`DrawPlayerSprites` and `DrawPSprite` draw it, with the weapon light (software lighting's curve, the
brighten a fullbright layer gives, bright fog), the 3D floor band the eye is in, the style and alpha
`AlterWeaponSprite` leaves, the overlay offsets and bob, and the targeter; recorded into the 2D capture
where GL's quad was. The bob is `P_BobWeapon`'s, which steps once a frame (`r_frameSerial`), so the
weapon bobs alike whichever renderers draw the frame. The weapon's style and alpha
are `ComputeWeaponStyle`, sharing the sprites' fuzz, transsouls and alpha1 rules, and a fullbright layer's
colour is `ComputeFullbrightColor`; `StyleOf` and `RenderStyleOf` carry an engine style to them and back.
`fua_psprite_check 1` compares the derived quads with GL's bit for bit (`fua_psprite_report`): about
54,000 frames with a weapon drawn across Freedoom MAP01 and MAP04 firing and the Whodunit maps, 0 differed. GL's own record
now carries a player-translated layer's colours and an additive layer's blend, which it dropped.

A weapon MODELDEF gives a model is a HUD model (`HUDModelLayer`): the layer's model frame captured over the
world, posed as `gl_RenderHUDModel` poses it (`ComputeHUDModelMatrix`, in eye space, then into the mesh with
`ComputeEyeToMesh`), in the layer's colour and light without fog, and drawn at the near end of the depth
range so the world never covers it, as GL draws it after clearing the depth buffer. Scenario `hudmodel` puts
a voxel box on the pistol: beside GL, standalone and without GL it covers the pixels GL's does.

`fua_sprite_check 1` derives every sprite and model beside GL's and compares the two bit for bit, every
vertex of a model surface included, matching an actor's pieces within one walk; `fua_sprite_report`
prints what differed and how many model surfaces were compared. It matched on about 1.6 million
actor-frames across AOW03, wdiex02, WDI01, WDI09, WDI18 and dbab04, with XY billboards, `r_spriteclip 2`
and `3`, `r_sprite_blend`, `r_fuzztype`, dynamic sprite lights, invulnerability and light amplification;
and with models on about 4.2 million more actor-frames and 103,000 model surfaces across the Whodunit
maps. In standalone the actors only a mirror or stacked view sees count as "only ours", since GL walks
neither there.

## In-engine hooks

- `features/frame/standaloneframe.cpp`, the sprite sweep: `BuildSprites` for the actors in view, derived in
  chunks on `features/jobs`' pool (a model, or a sprite whose material is not made yet, on the game thread)
  and appended in sweep order, or one at a time with a check on; `BuildSprite` for each actor a stacked
  view's or mirror's walk reaches; `BuildParticle` for each particle the player's walk reaches.
- `features/sidebyside/glchecks.cpp`: GL's sprites, models and weapon as GL reports them, for the checks.
- `features/levelmesh/flatmesh.cpp` `RegisterSprite`: `NoteGLSprite`, GL's piece for the check, and the
  `ThingColor` fold.
- `features/levelmesh/modelmesh.cpp`: `ModelPieceLook`, a model's look for GL's capture and ours;
  `CaptureModelSurface` notes GL's surfaces for the check (`NoteGLSprite`) and holds a checked derived
  model's (`NoteDerivedSprite`).
- `gl/models/gl_models.cpp` `gl_RenderModel` and `gl_RenderFrameModels`: `ModelPoseFor` with
  `ComputeModelMatrix`, `NextModelFrame`, and each slot's surfaces and blend from `ModelSlotSurfaces`.
- `features/models/md2model.cpp`, `md3model.cpp`, `voxelmodel.cpp`: `FrameSurfaces`, each model's
  surfaces worked out without GL and drawn from.
- `features/frame/playerview.cpp` `RenderPlayerView`, every frame the backend draws: `CheckFrameSprites` after
  the scene, then `BuildPlayerSprites`, `BuildTargeterSprites` and `CheckFramePSprites`.

The render settings it reads (`glset`, `gl_fixedcolormap`, `currentmapsection`, `getExtraLight`) are
`features/rendersettings`'; GL's sprite cvars (`r_spriteclip`, `r_sprite_blend` and the rest) are still GL's.
Models are `features/models`: the loaders, MODELDEF and each model's geometry, which `CaptureModelSurface`
lerps on the CPU.

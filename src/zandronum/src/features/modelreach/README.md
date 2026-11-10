# features/modelreach

A model can be far wider than the actor it stands on: a 768-unit slab on a 16-radius actor. Both renderers
decided whether to draw an actor from its centre alone, so such a model vanished whole once its centre left
the view, though most of it was on screen:

- GL and the backend's mirrors and stacked views draw the actors of the sectors their walk reached, and an
  actor is listed only in the sector its centre is in.
- The backend's main view drops an actor whose centre, padded by its radius and height, is outside the frustum.
- All of them drop an actor whose centre is in a map section out of view.

UZDoom has the same fault unless a mod sets `RenderRadius` by hand (uzdoom@87b23d160bcbfe8ed35d6ff0921b72f54109ae41).
Here an actor's reach comes from its models, and `RenderRadius` can only widen it, or below 0 hide the actor.

- `computation/modelreach_compute`: a model's reach from its vertices and its MODELDEF placement (scale,
  offsets, stretch, spin, tilt), split into what the actor's scale multiplies and what it does not; an actor's
  reach at its scale; whether it is wide (reach past its radius); the box of its footprint, the blockmap lines
  under it, the sectors they and its centre touch, and whether a walk reached any of them.
- `modelreach.cpp`: once per fill, the sectors under each wide actor the copy's take noted (`WideActors`), kept for an actor that
  has not moved; the sectors a walk reached (`MarkReached`), and the wide actors a walk that offers each
  reached sector's actors left out (`EachWideMissed`).

Every model is built once when MODELDEF loads, to measure it, and let go again; the first draw builds it as
before. The reach is per sprite, the largest of every class's model and VOXELDEF voxel on it, whatever
`r_use_models` and `r_drawvoxels` say, so it only ever errs wide.

Scenario `modelrenderradius` is the test: a slab whose centre is behind the view, and one poking through a wall
from a room of its own, in GL and Vulkan.

## In-engine hooks

- `actor.h` `renderradius`, `thingdef/thingdef_properties.cpp` DECORATE `RenderRadius`.
- `features/models/modeldef.cpp` `gl_InitModels`: `BuildSpriteReach`, read through `SpriteModelReach`.
- `features/gamecopy`: each record's `modelFootprint` and `modelReach` (`TakeActor`), `visible` false for a
  negative `RenderRadius`, `WideActors` noted by each chunk of the take, and `FillCount`.
- `features/frame/standaloneframe.cpp`: `PrepareWide` before the walks, the frustum test padded by
  `modelReach`, and the walks' sectors marked before the sweep.
- `features/sprites/spritebuild.cpp` `SubjectOf`: a reached footprint stands in for the centre's map section.
- `features/visibility/viswalk.cpp` `PortalSprites`: the view's sectors marked, and its missed wide actors handed out.
- `gl/scene/gl_scene.cpp` `CreateScene`: `PrepareWide`, then `gl_RenderWideThings` (`gl/scene/gl_bsp.cpp`)
  right after the walk, while the sectors it reached still carry its `validcount`; `GLSprite::Process` takes
  `footprint` to skip the centre's map section, and hides a negative `RenderRadius`.

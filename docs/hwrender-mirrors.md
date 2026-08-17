# Mirrors in the Diligent backend

Status: **planar mirrors reflect the level but not the actors.** Known incomplete.
Recorded here before diverging to a ray-traced approach, so the planar path is
documented rather than lost.

## What works

`Line_Mirror` (special 182) surfaces are found at level load, their geometry is built
straight from the linedef and the front sector's planes, and the level reflects
correctly — walls, floors and ceilings, at any viewing angle, clipped to the mirror
plane.

The reflection is planar: the camera is put through the mirror plane, the world is
rendered into a screen-sized target, and the mirror's quad samples that target at the
*same screen position*. That works because the reflected camera keeps the main camera's
projection, so a point on the mirror surface lands on exactly the pixel showing what it
reflects — no second projection to get wrong, and no per-mirror texture coordinates.

## What does not

**The reflection contains no actors.** Not the player, not monsters, not items.

`DrawWorld` draws the baked level mesh plus a sprite list, and that list is captured
from GL's BSP walk for the *player's* camera. A reflection needs the list for the
*mirror's* camera. The player's own sprite is not in the main list at all — you never
see yourself — so it can never appear in a reflection built from it.

The room reflects because the level mesh is view-independent. Nothing else can.

This is the same root cause as the wider "cut the GL dependency" item: the backend does
not decide what is visible, it replays a decision made for a different camera.

Reported as "the mirror is tracking the camera", which is what a mirror looks like when
the geometry reflects and the one thing that should move is absent.

## Two checks that could not fail

Worth recording so they are not repeated.

- A **whole-frame diff** of 2.8 mean per-pixel difference is dominated by the room
  around the mirror. It stays low while the mirror itself shows the sky.
- A **mean over the mirror's screen rectangle** is useless when that rectangle is
  uniformly dark: both renderers read about 12 whatever is drawn in it.

Both were reported as verification. Neither could have distinguished a working mirror
from a broken one. A test that cannot fail proves nothing.

## Fixes that are real and stay

- The reflected angle is `2*mirrorAngle - cameraAngle`, which is negative for a large
  share of camera angles. Converting that to `angle_t` — an **unsigned** type — is
  undefined behaviour, and produced a reflected camera pointing somewhere unrelated and
  lurching as the player turned. Now wrapped before conversion.
- The mirror's own direction is `atan2(nx, -ny)`, not `atan2(-nx, ny)`. Getting it
  backwards is invisible on an axis-aligned mirror — the two answers differ by exactly
  360 degrees there — and wrong on every other one.

## Why the next attempt diverges

Performance is not the constraint. The GPU does about 0.3 ms of work per frame on
Sunder MAP10, so an extra full-screen pass per mirror is affordable even at two or three
mirrors. The problem is entirely correctness.

Ray-traced reflections answer it better, and the hardware is present:
`VK_KHR_ray_query`, `VK_KHR_acceleration_structure` and `VK_EXT_descriptor_indexing`
are all exposed, and DiligentCore ships `BottomLevelAS` / `TopLevelAS`.

- Rays need no visibility list, so actors appear in reflections without solving the
  sprite-capture problem first.
- Recursion is free — a mirror facing a mirror just works, which the planar path cannot
  do at all.
- Cost scales with **mirror pixels**, not with a full-screen pass per mirror.
- The static level mesh is already one flat vertex buffer, which is exactly the input an
  acceleration structure wants. The expensive prerequisite is already built.

Costs, honestly: it needs bindless materials (a ray hit gives a primitive index, so the
shader picks its own texture), shading at the hit point by interpolating the baked
per-vertex attributes, an RT-capable GPU with the planar path kept as a fallback, and a
per-frame TLAS rebuild once actors are included.

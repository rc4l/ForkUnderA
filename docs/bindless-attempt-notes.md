# Bindless materials in the Diligent backend — what an attempt found

Written after building it, watching it fail four different ways, and reverting. The design is right
and the mechanism works; what stopped it is a size ceiling nobody has explained yet. This is the map
of the minefield so the next attempt starts past it.

## The design (unchanged, still right)

- `SceneVertex::lightIndex` already carries the **batch index** per vertex — it was put there for the
  ray-traced path. Slot *i* of the material array is batch *i*, so the handle already exists and
  costs nothing.
- `uSkyColor.w` carries the live slot count: zero means "use the bound texture", which makes the
  whole thing switchable from one constant with no second pipeline.
- `dgtexture.cpp` needs no changes at all. It already produces one Diligent texture per
  (FMaterial, translation) and hands out views.

## Four failures, in order, each with its lesson

1. **Array declared MUTABLE.** A mutable resource is bound per SRB, and this backend makes one SRB
   per material per pipeline — so a 64-slot array becomes tens of thousands of descriptors and the
   device dies during pipeline setup. It must be **STATIC**: the array is the level's material set,
   not one draw's state. (I diagnosed this correctly, wrote the fix, and the edit silently did not
   apply — then spent three cycles chasing a bug I had already fixed on paper. Check the file, not
   the intent.)

2. **Static slots left unassigned.** Diligent copies a pipeline's static resources into every SRB at
   creation and refuses — fatally — while any element is unassigned. Filling a hand-written list of
   "the world pipelines" missed five of the thirteen that share the resource layout. Fill inside the
   creation loop, right after each pipeline exists, with the white placeholder.

3. **Shader array size ≠ fill count.** `uMaterials[1024]` filled 512 slots produces exactly the same
   fatal, one message per empty slot. They are tied with a `static_assert` now; a GLSL string cannot
   read a C++ constant, so this can only ever be a convention plus an assert.

4. **A size ceiling between 64 and 128.** 64 slots runs. 128 does not. Past it the process dies
   during pipeline setup with **nothing in the log at any severity** — no Diligent error, no Vulkan
   validation line, the frame simply never arrives.
   - Raising `MainDescriptorPoolSize` / `DynamicDescriptorPoolSize`
     (`NumCombinedSamplerDescriptors`, `NumSampledImageDescriptors`,
     `NumSeparateSamplerDescriptors`) did **not** lift it — and setting those fields at all made a
     size that had previously worked stop working, which says they are not independent of the fields
     left at their defaults.
   - The device reports `bindless yes, sampler arrays yes`, and the ray-traced mirror pass has been
     shipping `sampler2D uMaterials[128]` since it landed. **128 works there and not here**, which is
     the single most useful fact in this document: the difference between those two pipelines is
     where the answer is. The mirror PSO declares its array as static on ONE pipeline; the world
     declares it on THIRTEEN that share a resource layout.

## Where to start next time

Compare the mirror pipeline against a world pipeline, since one takes 128 slots and the other will
not. Suspects, in order: the thirteen-pipeline resource layout multiplying the static cache; a
per-stage sampled-image limit reached by the sum rather than by any one pipeline; and
`ShaderResourceRuntimeArrays` — an unbounded array may be the supported path where a large fixed one
is not, which would also remove the fixed ceiling entirely.

A minimal repro outside the engine — one Diligent device, N pipelines sharing a layout, one static
sampler array — would answer this in minutes and is worth writing before touching the backend again.

## What is not in doubt

The port does **not** need UZDoom's texture system for any of this (see
`docs/texture-system-decision.md`). Nothing in the four failures above is about where the pixels come
from.

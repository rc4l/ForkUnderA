# Bindless materials in the Diligent backend

**Done, on by default.** The world picks its own texture out of an array and binds nothing per batch;
thirteen bindings serve the level, sprites and decals included, and adjacent batches collapse into
one draw because a batch is then only a range.

Sunder MAP16, same view: **165 draw calls at 0.77 ms become one at 0.49**. Pixel-identical on
MAP10, MAP16, dbab01, dbab02 and dbab04 — 0.0% over the world region, against controls that also read
0.0% — both loaded directly and reached through a chain of four map changes.

## The bug that held it back, and what it was

For a day this looked like two separate faults: after a *map change* the world rendered with the
wrong textures (55-93% of the frame, geometry perfectly intact), and sprites that appeared later drew
as flat white rectangles. They were one bug.

`uMaterials` is a **static** shader variable, so Diligent stores it on the pipeline and copies it into
every binding created from that pipeline. **Once a pipeline has handed out a binding, writing the
array again does not take** — it keeps whatever it held when the first binding was made. A map loaded
directly was always fine, because there the array is filled before any binding exists. A map *changed
into* filled the array and got the previous level's textures. A sprite whose material joined the
table after the fill could not be added at all, so its slot stayed white.

The fix is to make the pipelines again whenever the array has to change: `RefreshBindless` releases
the bindings, releases the pipelines, rebuilds them, and fills before anything asks for a binding.
That is rare — the table settles within a second of a level starting.

Eight other suspects were measured and cleared before this one, and it was worth writing them down
because every one of them was plausible: the slot table (`fua_dg_slots` shows the batch material, the
table slot, and the slot the *vertex itself* carries, all agreeing in the broken state), the array's
contents, the texture cache, a stale array, animated textures, the draw merge, slot renumbering, and
drawing blended batches the old path skipped.

One of those, though, was a real bug on its own: **slot numbers used to be renumbered on every scene
rebuild**, and a level with moving sectors rebuilds a couple of hundred times a minute. They are
append-only within a level now, which took array builds from 340 to 35.

What follows is the record of the attempt that failed first, kept because every one of its four
failures is a trap the next person walks into in the same order.

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

4. **A size ceiling between 64 and 128 — solved, and it was never a pool size.**

   64 slots ran; 128 killed the device during pipeline setup with nothing in the log at any
   severity. Raising `MainDescriptorPoolSize` / `DynamicDescriptorPoolSize` did not lift it and made
   a previously-working 64 stop working.

   The reason is architectural, and it is visible from Diligent's own contract rather than from a
   repro. A `SHADER_RESOURCE_VARIABLE_TYPE_STATIC` variable is **copied into every SRB created from
   the pipeline**. This backend creates one SRB per `(pipeline, material, translation)` —
   `g_matSRBs` in `dgscene.cpp`, deliberately, because a MUTABLE variable is baked into the
   descriptor set at commit time and cannot be re-pointed per draw. So an N-slot array does not cost
   N descriptors. It costs **N times the number of SRBs**.

   `fua_dg_srbcost` prints the multiplier for the loaded map. On Sunder MAP10:

   ```
   material SRBs live: 149 cached + 0 batch = 149
   a 128-slot STATIC sampler array would add 19072 combined-image-sampler descriptors
   one SRB per pipeline instead -- which is what bindless is FOR -- would add 1664
   ```

   64 slots is 9536 descriptors and 128 is 19072, which brackets Diligent's default
   `NumCombinedSamplerDescriptors` of 8192 per pool exactly where the ceiling was observed. The
   mirror pass takes 128 slots happily because it has **one** SRB, not one per material.

   **So bindless and per-material SRBs are mutually exclusive by construction**, and the first
   attempt did them in the wrong order: it added the array while keeping the SRBs, which multiplies
   the descriptor cost by the material count instead of dividing the draw cost by it.

## How it was finally done

Do the two halves together, in this order:

1. **Retire the per-material SRB for world geometry.** One SRB per pipeline, with the material array
   bound once and `uTex`/`uBrightmap` pointed at the white placeholder. `g_matSRBs` stays for
   anything not yet on the bindless path, and `fua_dg_srbcost` says when it is empty.
2. **Then** grow the array to the level's material set. With one SRB per pipeline the cost is
   `slots x 13`, which is 6656 descriptors at 512 slots — an order of magnitude under where the
   ceiling was, and flat in the number of materials rather than linear in it.

`ShaderResourceRuntimeArrays` (an unbounded array) remains the tidier long-term shape, and the device
reports it; it is an optimisation of step 2, not a prerequisite for it.

## What is not in doubt

The port does **not** need UZDoom's texture system for any of this (see
`docs/texture-system-decision.md`). Nothing in the four failures above is about where the pixels come
from.

## And the third item on #311: measured, not built

The issue asked for GPU frustum and occlusion culling feeding an indirect draw buffer. The scale
probe says there is nothing left for it to remove.

```
Diligent scale probe: 266402 tris/copy, 165 batches
   1x =    266402 tris,   165 draws -> GPU 0.1555 ms
  10x =   2664020 tris,  1650 draws -> GPU 0.4664 ms
 100x =  26640200 tris, 16500 draws -> GPU 3.3061 ms
=> ~8058015 tris per GPU millisecond at this resolution and shader
```

Sunder MAP16's entire world is 266,402 triangles, drawn in **one** call, for **0.1195 ms** of GPU
after the vertex shrink. Perfect culling could save at most that tenth of a millisecond, and a
compute dispatch to decide it would cost some of it back. Per-piece indirect draws would go the wrong
way outright: the probe's own numbers show draw count is not free, and 110,731 pieces is not 165.

The reason the issue expected a win was the per-batch submit cost -- 0.445 ms on 166 batches -- and
that cost is gone, removed by bindless rather than by culling. `fua_dg_cullbatches` stays off for the
same reason it always was, but the reason has changed from "it makes things worse" to "there is
nothing there".

Worth revisiting only if something makes the GPU expensive: real shadows (#306), much higher
resolution, or a shader far heavier than this one.

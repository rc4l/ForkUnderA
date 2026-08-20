# Bindless materials in the Diligent backend

**Built, measured, and off by default** — `fua_dg_bindless 1` turns it on. The world picks its own
texture out of an array and binds nothing per batch; thirteen bindings serve the level, and adjacent
batches collapse into one draw because a batch is then only a range.

Measured on Sunder MAP16, same view: **165 draw calls at 0.686 / 0.715 / 0.689 ms become one at
0.499 / 0.503 / 0.565 ms**. Sunder MAP10: 53 draws at 1.68 / 1.56 ms become one at 1.52 / 1.50 ms.
Pixel-identical on the frozen frame on MAP16, MAP10 and dbab01 — 0.0% over the world region, with
both controls (off/off and on/on) also at 0.0%.

## Why it is not on

**After a map change the world renders with the wrong textures** — 55-93% of the frame depending on
the level, on dbab02 as well as dbab04, so it is the transition and not the map. Loaded at launch and
toggled on, every map tested is **0.0%** — pixel-identical, draw merge and all, against controls that
also read 0.0%. Deterministic in both directions.

The geometry is perfect. Only the textures are wrong, and the two screenshots line up wall for wall.

### What has been ruled out, each by measurement

| Suspect | How it was killed |
|---|---|
| The slot table | `fua_dg_slots`: batch material, table slot, and **the slot the vertex itself carries** all agree, name for name, in the broken state |
| The array's contents | The fill records what it was handed: correct names, fresh views for the new level |
| The texture cache | `fua_dg_flushtextures` drops it, every binding, and forces a rebuild — no change |
| A stale array | `fua_dg_bindless_rebuild` — no change |
| Animated textures | Wrong with `fua_dg_bindless_anim 0`, and wrong with animation off entirely |
| The draw merge | Identical wrong with `fua_dg_mergedraws 0` |
| Slot renumbering | Fixed (below) — the churn went from 340 array builds to 35, the picture did not change |
| Drawing blended batches that used to be skipped | Guard restored — no change |

### What was fixed along the way

**Slot numbers are stable for the life of a level now.** They were reset on every scene rebuild, and
a rebuild renumbers from the new emit order — so a material that was slot 3 became slot 7 while every
binding built from the old numbering was describing a different level. On dbab04 that happens about
two hundred times a minute. The table is append-only within a level and reset only on a level change.
This is right whether or not it is the bug, and it took array builds from 340 to 35.

### Where to look next

Everything the CPU can be asked says the frame should be correct, so the next step is to ask the GPU:
a capture (RenderDoc) at a draw in the broken state answers in one look what is actually in the
descriptor set and what index the vertex carries. That is the tool this needs, and it is the first
thing I would reach for rather than another round of counters.

The second open item is smaller and separate: **sprites** get a valid slot and take the array, and
some — items, torch flames — still draw as flat white rectangles. The dynamic path therefore writes
slot 0, which sends it back to the bound texture and the path it was always on.
`fua_dg_bindless_dyn 1` puts it back on the array.

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

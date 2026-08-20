# Bindless materials in the Diligent backend

**Built, measured, and off by default** — `fua_dg_bindless 1` turns it on. The world picks its own
texture out of an array and binds nothing per batch; thirteen bindings serve the level, and adjacent
batches collapse into one draw because a batch is then only a range.

Measured on Sunder MAP16, same view: **165 draw calls at 0.686 / 0.715 / 0.689 ms become one at
0.499 / 0.503 / 0.565 ms**. Sunder MAP10: 53 draws at 1.68 / 1.56 ms become one at 1.52 / 1.50 ms.
Pixel-identical on the frozen frame on MAP16, MAP10 and dbab01 — 0.0% over the world region, with
both controls (off/off and on/on) also at 0.0%.

## Why it is not on

**After a map change, dbab04 draws almost every surface with some other surface's texture** — 82-93%
of the frame, mean |d| 26-47. Not white, not missing: wrong.

The transition is the whole of it, and that took a while to see:

- Loaded at launch and toggled on, dbab04 is **0.0%** — pixel-identical, with the draw merge on as
  well. So is dbab01, so are Sunder MAP10 and MAP16.
- Reached by `map dbab04` from another level, it is wrong every time, whether bindless was on across
  the change or switched on afterwards.

What has been measured and is NOT the cause: the slot census (108 slots, none falling back to white,
all thirteen pipelines filled and bound, every draw on the shared binding); animated textures (wrong
with animation off too, and the resolution scan was rewritten once already — see
`UpdateMaterialSlotResolutions` for the version that got it wrong and why); the draw merge (it runs
with bindless off as well, and that path is byte-identical to the old one); a stale array (forcing a
rebuild with `fua_dg_bindless_rebuild` changes nothing).

And the thing that narrows it hardest, from `fua_dg_slots`: **the batch's material, the slot the
table gives it, and the slot the vertex actually carries all agree**, name for name, in the broken
state. So the index is right at every step and the fault is in the last link — what
`GetMaterialSRV` hands back for a correct material pointer.

That link has a known hazard, documented at `AutoSetupForLevel`: the texture cache is keyed on raw
`FMaterial*`, and a new level's materials land at the addresses the old level's just vacated.
`ReleaseMaterials` exists for exactly that. Bindless calls `GetMaterialSRV` from a different place
and at a different time than the per-material path did, so the next thing to do is find which of
those calls happens while the pointers are in flight — the guard on `g_bindlessGen` was an attempt at
that and it is not sufficient.

The second open item is smaller and separate: **sprites** get a valid slot and take the array, and
some of them — items, torch flames — still draw as flat white rectangles. The dynamic path therefore
writes slot 0, which sends it back to the bound texture and to exactly the path it was on before.
`fua_dg_bindless_dyn 1` puts it back on the array.

Diagnostics to start from, all already there: `fua_dg_srbcost`, `fua_dg_slots` (batch material vs
table slot vs what the vertex carries), `fua_dg_bindless_rebuild`, the bindless section of
`fua_dg_dynstats`, and debug views `fua_dg_lightmode 16` (material slot as a grey ramp) and `17` (is
this fragment taking the array).

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

# Do we need UZDoom's texture system to finish the port?

**No — not for the Vulkan backend, and not for bindless materials.** #4 is real debt, but it is debt
on a different path than the one the modernization program is finishing, and doing it now would buy
almost none of what it was blocking.

This is the check before committing to a 44,101-LOC port across 74 files with 124 call sites to
migrate.

## What #4 actually is

#4 replaces the `FTexture`/`FMaterial` → `FGameTexture`/`FMaterial` adapter that exists so the
**ported UZDoom GL backend** (`features/hwrender/backend`) can call `ApplyMaterial`. That backend
expects UZDoom's material model: `Source()`, `GetShaderIndex()`, `NumLayers()`, `GetLayer()`. Our
adapter answers those from Zandronum's objects and stubs the parts we have no equivalent for — it
reports "no layers" and "identity detail scale".

That is a genuine problem *for that backend*, and it is why we could not take UZDoom's `F2DDrawer`
to replace our 68 legacy 2D draw calls.

## What the Vulkan backend actually uses

Nothing of it.

`features/hwrender/diligent/dgtexture.cpp` meets the engine at exactly one seam:

```
unsigned char *buf = mat->CreateTexBuffer(translation, w, h, true, false);
```

RGBA bytes in, a Diligent texture out, cached by `(FMaterial*, translation)` in a flat array. It also
reads three flags — `bWarped`, `bHasCanvas`, `gl_info.Brightmap` — and nothing else. The layer
machinery, the shader indices, the upscaler, the hardware-texture objects: none of it is on this
path, because Diligent owns the GPU-side texture and the engine owns only the pixels.

So porting 44k LOC would change how those bytes are *produced* and leave the backend's use of them
identical.

## What bindless actually needs

The thing #311 is blocked on is not a texture system. It is four items, and three of them exist:

1. **A stable index per material.** `g_textures` in dgtexture.cpp is already an array; its index is
   the handle.
2. **Every texture reachable from one descriptor set.** Diligent supports this —
   `PIPELINE_RESOURCE_FLAG_RUNTIME_ARRAY`, and the `BindlessResources` /
   `ShaderResourceRuntimeArrays` device features, which **dgprobe.cpp already requests**.
3. **A shader that can index it.** Already written and shipping: the ray-traced mirror pass declares
   `uniform sampler2D uMaterials[128]` and indexes it with `nonuniformEXT`, with its own immutable
   sampler. A ray can land on any triangle in the level, so that path needed bindless first and
   proved the whole mechanism — device feature, GLSL extension, sampler, per-slot binding.
4. **A material index per surface, readable by the raster shader.** This is the only missing piece.
   Vertices already carry a batch index in `SceneVertex::lightIndex` for exactly this reason on the
   RT side.

So the Vulkan-native answer is to generalise what the mirror pass already does to the raster pass:
grow the array from 128 mirror slots to the level's material set, carry the index per piece, and stop
binding a material per batch.

## What that unblocks, and what it does not

Unblocks, all in #311:

- Per-batch material binding disappears, which is the CPU cost the culling experiment could not
  remove (measured: 166 batches, 0.445 ms of submit on Sunder MAP16).
- Per-piece indirect draws become possible, and with them GPU culling that is actually worth doing —
  the reason batch culling removed only 20 of 2348 batches is that a batch spans the level, and a
  batch only spans the level because it exists to share one material binding.
- Sprite instancing (moved here from #305), which needs runs of sprites sharing a material and cannot
  have them while the translucent pass sorts by distance.

Does **not** unblock, and stays with #4:

- `F2DDrawer` and the 68 legacy 2D draw calls.
- The ported UZDoom GL backend's material path and its stubbed layer/detail information.
- Anything that wants UZDoom's texture *features* — layers, detail textures, the upscaler.

## The decision

Do the bindless work in the backend now; leave #4 as the debt it is, on the path it belongs to.

The cost comparison is not close: a few hundred lines in `dgtexture.cpp` and the scene shaders,
against 44k LOC across 74 files plus a 124-file migration. And the risk runs the other way from what
the issue titles suggest — the texture port touches every part of the engine that names a texture,
while the bindless change touches one backend that is already switchable off (`fua_vulkan 0`) and
already carries an A/B against GL.

If UZDoom's texture features are wanted later — layers, detail, upscaling — that is a reason to do
#4, and a better one than "bindless needs it", which turned out not to be true.

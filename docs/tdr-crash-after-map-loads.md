# The crash after N map loads: it is a GPU TDR, and it is on the Vulkan path

Reported as "the engine dies after about eleven consecutive map loads", and originally as a crash on
`wdiex02`. Chased through six wrong theories before the evidence settled it. This is what is proven,
what is not, and what was tried and failed -- so the next attempt starts from the right place.

## What it is

**Every failure is a Windows GPU Timeout Detection and Recovery.** The Application event log carries
one per observed failure -- seventeen in a ninety-minute window, each naming the dying process:

```
NVIDIA OpenGL Driver: A TDR has been detected. The application must close.
Error code: 7  (pid=77748 tid=25996 forkundera.exe 64bit)
```

The driver terminates the process from outside. That is why **nothing in the engine can report it**:
no exception is raised, no dump is written, and the log simply stops mid-line. Six different handlers
were added chasing this and all of them stayed silent, correctly -- they catch a process that stops,
and this one is killed.

## It is the Vulkan path, not GL

| configuration | result |
|---|---|
| `fua_vulkan=1` (default), pre-fix engine | failed 1 sequence in 3 |
| `fua_vulkan=1`, with the ReviveSlot/free-list/thread fixes | failed 1 in 4 -- **unchanged** |
| **`fua_vulkan=0`** (pure GL, no Diligent) | **survived 4/4 sequences** |

A sequence is 24 consecutive map changes across the Whodunit set. The NVIDIA *OpenGL* component
reports the TDR because the process holds a GL context, but the GPU reset is process-wide and the
work that provokes it is the backend's.

## The mechanism, as far as it is established

Textures are created lazily, so the frames after a level change materialise every texture the map
uses -- `wdi24` resolves ~1800 material batches. Each one is an `UpdateTexture` plus a
`GenerateMips`, and nothing flushes between them, so they land in a single command list submitted at
Present. Past roughly two seconds of GPU work Windows resets the device. The worst frame measured in
this work was **2,421 ms**, right on the threshold.

That is consistent with everything observed -- cumulative across loads, worse on big maps, always at
a level transition -- but it is **not proven**, because the two attempts to act on it both failed.

## What was tried and did NOT work

1. **Flush every 8 texture uploads, inside the upload itself.** Faulted immediately: the HUD also
   creates textures mid-frame, and flushing ends the render pass a `Draw` is standing in. Caught with
   a full symbolised stack in `DeviceContextVkImpl::Draw` under `Draw2D` -- by the very handlers added
   while chasing this, which is the one place they paid for themselves.
2. **Flush every 16 uploads inside `FillMaterialArraySRB`** (the bulk loop, nothing drawing). No
   fault, but it turned one long submit into ~90 short ones and the loader stalled: GL precache time
   went 44 ms -> 668 ms and the bridge stopped answering during level load.
3. **A per-frame budget on new textures**, leaning on the existing white-fallback-and-retry
   machinery. Also stalled -- each retry rebuilds every world SRB, so ~19 passes over a big map is
   pathological.

All three are reverted. The engine at HEAD is the pre-attempt behaviour, verified healthy.

## What to try next

The budget idea is still the right shape -- spread the uploads over frames so no single submit is
long -- but it must not drag a full SRB rebuild along with it. Either make the retry path cheap (fill
only the slots that changed rather than releasing and rebuilding every binding), or materialise
textures from a queue drained outside the binding fill entirely.

Worth measuring first, and cheap: **how long `GenerateMips` actually accounts for.** If it dominates,
generating mips only above a size threshold may drop the submit under the timeout with no
restructuring at all.

## FOUND AND FIXED: the planar mirror pass

`RenderMirrors` re-rendered the entire scene **once per mirror, unconditionally** -- no visibility
test of any kind. These maps carry six mirrors against ~50,000 pieces, so a frame was seven full
scene renders. At a level transition, with every material resolving for the first time and the batch
layout not yet settled, that is enough GPU work in one submit to cross the two-second timeout.

The fix uses a number the loop was **already computing and then ignoring**: `dist`, which side of the
mirror plane the camera is on. Negative means you are behind the mirror -- the surface faces away,
nothing of it can be seen, and rendering the world into it cannot contribute a pixel.

```cpp
const float dist = m.nx * px + m.ny * py - m.d;
if (dist <= 0.f) continue;      // behind it: nothing to see, nothing to draw
```

### Measured, same build, same session, back to back

| configuration | sequences | failures | TDR events |
|---|---|---|---|
| mirrors off (`fua_dg_mirrors_on 0`) | 4 | 0 | 0 |
| mirrors on, no cull | 4 | **2** | **2** |
| **mirrors on, with the cull** | **6** | **0** | **0** |

A sequence is 24 consecutive map changes across the Whodunit set. The fixed build ran **240 map loads
with mirrors enabled and not one TDR**, against a baseline that failed about one sequence in three.
The TDR timestamps line up exactly with the control run and stop dead at the fix.

### The picture is unchanged

- dbab02 and dbab04 Vulkan captures: **0.1%** against the stored baselines, their own reload floor.
- Mirrors still render: toggling the pass off changes up to **73.6%** of the frame at a vantage
  facing one, so the cull removes invisible work and nothing else.

`fua_dg_mirrors_on` is kept. It is what made this bisectable at all -- every other suspect could be
switched off and measured, and this one could not, which is the only reason it survived nine rounds
of elimination.

## Everything ruled out by measurement

Each of these was tested against the real failure, not reasoned about. None of them is the cause.

| suspected | test | result |
|---|---|---|
| GL texture precaching | `cl_fua_caching=0` | still failed at load 22 |
| `GenerateMips` (a render pass per level, ~12,600 per submit) | `fua_dg_mips=0`, no chain at all | still failed at load 22, 2 TDRs |
| VRAM exhaustion | `nvidia-smi` after each of 23 loads | engine adds ~520 MiB once, then ~7 MiB/load; peaks at 11.8 of 16.3 GB with 4.5 GB free |
| Unbounded light loop in the scene shader | clamped `count` and `li` | still failed at load 22, 3 TDRs |
| Process memory | working set per load | ~30 MB/load, tops out ~1.6 GB, same with the backend off |
| Thread leak | thread count per load | flat at ~124 |

The shader clamp was **kept anyway**: taking a fragment-shader loop bound straight out of a GPU buffer
with no limit is a real latent hazard whatever provoked this particular TDR, and the clamp costs one
instruction. It is not a fix and is not presented as one.

## Where that leaves it

A TDR means one GPU operation ran past about two seconds. It is not the uploads, not the mip chain,
not memory pressure, and not the light loop. What is left, unexamined:

- **The planar mirror pass.** These maps log `6 mirror surface(s)`, and a planar mirror re-renders the
  scene per mirror. Six mirrors against 50,321 pieces is six times the work, and there is no cvar to
  switch it off for a bisect -- adding one is the cheapest next experiment in this document.
- **Acceleration structures.** `ReleaseAccelerationStructures()` runs per level; whatever builds them
  has not been timed.
- **A pathological draw** on the first frame of a new level, before the batch layout settles.

The bisect that would narrow it fastest is a per-pass GPU timer around the level-transition frames --
`gl-timers` already reports per-pass GPU ms for the steady state, and the same instrument pointed at
the first frames after a map change would say which pass eats the two seconds, instead of another
round of guessing.

## Nine wrong theories, and why they were wrong

Recorded because each cost real time and each had a specific methodological cause.

1. **"A vertex arena leak."** `MeshStore`'s size-change path never runs on the map in question --
   `g_reallocs` is 0. Built a free list before measuring.
2. **"wdiex01 degrades over time."** `fua_frametimes` keeps a 1024-frame ring, so a reading taken
   right after a map change is still full of the previous map's frames.
3. **"A thread leak."** Threads are flat at ~124.
4. **"Heap corruption / fail-fast."** No exception of any code is raised; a vectored handler
   covering every fatal NTSTATUS never fires.
5. **"The bridge watchdog reaping it."** It announces itself now, and never did.
6. **"A hang in level setup."** Real, and observed once -- the game thread spinning at 100% of a core
   -- but it is a *consequence*: the TDR kills the process, the harness gives up, and a hung engine
   and a killed engine look identical from outside.

And one contamination: **two copies of the test harness were running**, each ending its iteration
with `Stop-Process -Name forkundera`, so the runs were killing each other's engines. `pkill -f` does
not match reliably under MSYS. Several early conclusions were drawn on those runs and had to be
thrown away. The harness now aborts if an engine is already running and quits only its own instance
by port.

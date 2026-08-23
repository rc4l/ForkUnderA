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

## Six wrong theories, and why they were wrong

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

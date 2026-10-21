# features/wipe

The screen wipes (melt, crossfade, burn) as GL's wipers run them, worked out once so GL and a launch without
GL draw the same wipe.

`computation/wipe_compute`, tested:

- `ComputeMeltStart`: each of the melt's 320 columns starts up to 15 tics late, within a tic of its neighbour,
  from one random byte a column drawn in column order as GL draws them;
- `ComputeMeltTic`: a tic of the fall, a row more each tic until it falls eight at a time;
- `ComputeMeltStrip`: where a column shows the old screen, shifted down by its fall;
- `ComputeCrossfadeAlpha`: the new screen's opacity, done at 32 tics;
- `ComputeBurnMask`: the burn's opacity from `wipe_CalcBurn`'s fire.

`computation/wipetrace_compute`, tested, behind `fua_wipe_trace` (`wipetrace.cpp`, for tests: each presented frame
is read back): `ComputeThumb` shrinks a presented frame to 32 by 20, `WipeTraceLog` keeps each wipe's frames with
the frame before and the first after, and `ComputeWipeDistances` says how far each frame sits from those two.
`fua_wipe_report` prints how many wipes began and how many the backend presented, then each frame's distances,
which scenario `wipe` holds to a melt from one picture to the next.

## In-engine hooks

- `gl/system/gl_wipe.cpp` `Wiper_Melt`, `Wiper_Crossfade` and `Wiper_Burn`: GL's wipers drawn from these.
- `wiperandoms.cpp` `DrawMeltRandoms`: the melt's bytes of `M_Random`, behind the `wipe/randoms` seam, which only the main thread may enter while no tic runs, which `dgwipe.cpp` draws from.
- `features/hwrender/diligent/dgwipe.cpp`: the backend's wipes, from `DiligentFrameBuffer`'s `WipeStartScreen`,
  `WipeEndScreen`, `WipeDo` and `WipeCleanup`.
- `d_main.cpp` `D_Display`: `NoteWipeStarted` for every wipe begun, presented or not.
- `features/hwrender/diligent/dgscene.cpp` `PresentFrame`: each frame about to be presented, to `TracePresented`,
  while `fua_wipe_trace` is on.

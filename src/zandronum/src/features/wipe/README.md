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

## In-engine hooks

- `gl/system/gl_wipe.cpp` `Wiper_Melt`, `Wiper_Crossfade` and `Wiper_Burn`: GL's wipers drawn from these.
- `features/hwrender/diligent/dgwipe.cpp`: the backend's wipes, from `DiligentFrameBuffer`'s `WipeStartScreen`,
  `WipeEndScreen`, `WipeDo` and `WipeCleanup`.

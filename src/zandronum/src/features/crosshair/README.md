# features/crosshair — Crosshair Options

Options → HUD Options → **Crosshair Options**. One home for every crosshair setting, and the place where
anything drawn at or next to the crosshair is configured.

The stock crosshair rows moved here from HUD Options unchanged: `crosshair`, `crosshairforce`,
`crosshairgrow`, `crosshaircolor`, `crosshairhealth` and `crosshairscale`. `crosshairscale` is now labelled
"Scale crosshair with resolution" so it doesn't read like the size slider.

Client-side display only: nothing here touches aiming, the network, savegames or demos.

## Files

| file | role |
|------|------|
| `crosshair.h/.cpp` | Engine glue: the CVARs and `zx::crosshair::GetRect`, which reads the image, the stock cvars and the 3D view's window. |
| `computation/crosshair_compute.{h,cpp,_test.cpp}` | Pure math: the clamps and `ComputeCrosshairRect`. 100% unit coverage. |

## Cvars

All are `CVAR_ARCHIVE | CVAR_GLOBALCONFIG` and clamped on set.

- `cl_fua_crosshair_scale` (float 0.25–4, default 1) multiplies the crosshair's size, on top of `crosshairscale` and `crosshairgrow`.
- `cl_fua_crosshair_x` and `cl_fua_crosshair_y` (float −50 to 50, default 0) move the crosshair by a percentage of the 3D view from its centre. Positive is right or down, and ±50 reaches the view's edge.

## How it is drawn

- `ComputeCrosshairRect` runs the stock size maths on the same 64-bit fixed-point values and with the same `Fixed64Mul` as `FixedMul`. At scale 1 and offset 0 it is bit-identical to the old inline code, and a test pins that against a copy of the old formula.
- The offsets are relative to the 3D view, not the screen. With the status bar showing (`screenblocks` 10), the centre is the middle of the area above the bar, as it always was.
- The offset moves the reticle only. Shots and the target identifier still use the view's centre.

## In-place anchors (the only edits outside this feature)

1. `g_shared/shared_sbar.cpp`: an include, and in `DBaseStatusBar::DrawCrosshair` the inline `size`/`w`/`h`
   maths and the centre point are replaced by `zx::crosshair::GetRect (CrosshairImage, CrosshairSize)`. Every
   gate before it is untouched. The alternative HUD draws through the same function, so it gets the sliders too.
2. `CMakeLists.txt`: `features/crosshair/crosshair.cpp` is in the zdoom source list. It has no `IMPLEMENT_CLASS`,
   so its position doesn't matter. The `*_compute.cpp` is picked up by the existing glob.
3. `wadsrc/static/menudef.txt`: the `Crosshair Options` row in `HUDOptions` and the `FUACrosshairOptions` menu.
   The six stock rows were removed from `HUDOptions`.

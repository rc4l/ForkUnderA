# touch-input

The on-screen controls a touch screen shows in play: a stick, a look drag and buttons, defined by the `FUATOUCH` lump, drawn by the engine above the HUD, and fed by whichever host has a touch screen (the browser page today; a handheld host would fill the same seam from SDL's finger events). The page itself holds no control: it forwards pointers.

## The `FUATOUCH` lump

Every `FUATOUCH` lump is read in load order, so a mod's layers on the core pk3's (`wadsrc/static/fuatouch.txt`): a `button` whose id is already there replaces it, a new id adds one, and `clear` drops everything before it.

```
stick { zone 0.00 0.30 0.42 1.00  radius 64  deadzone 0.20 }   // where a thumb may land to take the stick; radius in virtual pixels; dead zone as a fraction of it
look  { zone 0.42 0.00 1.00 1.00  sensitivity 1.0 }           // where a finger aims; mouse units a pixel of drag is
button fire { key rtrigger  at 0.90 0.78  size 84  label "FIRE" }
button nuke { command "+altattack"  at 0.70 0.80  size 60  label "ALT"  icon "TCHNUKE" }
portrait { button fire { at 0.86 0.84  size 90 } }             // what changes held upright; landscape { } likewise
```

- Zones and `at` are fractions of the view inside its safe area; `radius` and `size` are virtual pixels on the freeform menu's 480x270 canvas (`ScaleFac`: the smaller of width over 480 and height over 270), scaled by `fua_touch_size`; the labels scale the same way, so what fits a pill in the lump fits it on every screen.
- `key` is a `DEFBINDS` key name, so the Controls menu rebinds it, and the pad defaults already bind `rtrigger` (fire), `pad_a` (use), `pad_y` (jump), `lshoulder`/`rshoulder` (weapons), `dpadup` (map) and `pad_back` (menu).
- `command` instead takes the next spare joystick key (`joy9` up to `joy16`, in order of appearance) and binds it unless the player already has.
- `icon` names a graphic lump drawn in place of the label; `label` defaults to the id.
- A finger keeps the role it arrived with: a button's finger slides between buttons but never takes the stick; a second thumb in the stick's zone is a button finger; a tap between two tics still presses for one.

## Files

- `computation/touchdef_compute`: the layout model, how lumps layer (`MergeTouchButton`, `MergeTouchOverride`, `ClearTouchLayout`), where the controls lie on a view (`ComputeTouchRects`, insets, orientation overrides, left-handed mirroring), which pointer drives what each tic (`ComputeTouchFrame`) and when the overlay shows (`ComputeTouchShown`: in play alone, and wanted), tested.
- `computation/touchinput_compute`: the stick's pixels as joystick axes (`ComputeTouchAxes`) and a drag's pixels as mouse motion (`ComputeTouchLook`), tested.
- `touchdef.cpp`: the `FScanner` parse of every lump into the layout (`LoadDefs`, `Layout`).
- `touchjoystick.cpp`: the stick as an `IJoystickConfig` "Touch" the joystick menu lists and `I_GetAxes` sums, with the joystick's own dead zone, sensitivity and axis map, persisted by `M_LoadJoystickConfig`.
- `touchoverlay.cpp`: each tic the pointers into the stick, `EV_Mouse` look motion and `Joy_GenerateButtonEvents` key events; each frame the pills, labels or icons, and the stick; `fua_touch` (off, on, auto: shown once a touch is seen, hidden by a keypress until the next touch), `fua_touch_size`, `fua_touch_opacity`, `fua_touch_look`, `fua_touch_invert`, `fua_touch_lefthanded`; `fua_touch_status` for the checks.

## In-engine hooks

- `posix/sdl/sdlhost.h`: `TouchPointer`, `TouchView`, `ReadTouch` — the host's pointers in canvas pixels with the view's insets and orientation; `features/web/webhost.cpp` reads `Module.fuaTouch` (the page's `web/touch.js`), `posix/sdl/sdlhost_desktop.cpp` has none.
- `posix/sdl/i_input.cpp`: `I_GetEvent` runs `Tick` beside `MouseRead`; a key down calls `NoteKeyboard`.
- `posix/sdl/i_joystick.cpp`: `I_GetAxes` adds the stick, `I_GetJoysticks` lists the device once a touch has been seen.
- `c_bind.h`/`.cpp`: `C_KeyFromName`, the `DEFBINDS` key names for the lump.
- `d_main.cpp`: `D_DoomMain` calls `LoadDefs` after the wads are loaded and `GameConfig->DoKeySetup` has set the bindings (so a command's spare key respects the player's); `D_Display` draws the overlay before `C_DrawConsole` and `M_Drawer` at both sites, on the main thread outside the recorded HUD.
- `src/CMakeLists.txt`: the three `.cpp`s before `zzautozend.cpp`; `*_compute.cpp` globbed.
- `wadsrc/static/fuatouch.txt`: the default lump, in the core pk3.
- `wadsrc/static/menudef.txt`: the Touch controls page.

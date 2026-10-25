# Sound amplification (`$fua_allow_amplify`)

Lets a mod play a sound louder than its file, up to 2.0 (+6 dB). It is opt-in per SNDINFO lump, so a mod
that already passes a volume above 1.0 still plays at 1.0, as on Zandronum and GZDoom.

```
$fua_allow_amplify 1
weapons/bigshot     DSBIGSHT
$volume weapons/bigshot 1.5
$fua_allow_amplify 0
```

- `$fua_allow_amplify <0|1>` works like `$pitchshiftrange`. A sound may go above 1.0 when the last line of
  that lump that defined it, or set its `$volume`, came while the setting was 1. Each SNDINFO lump starts
  with it off, so one mod's setting never reaches another's sounds.
- The volume a sound plays at is the call's volume times its `$volume`, held to 0..2.0 when amplified and
  0..1.0 otherwise. The call's volume comes from `A_PlaySound`, `A_StartSound`, ACS `PlaySound`,
  `PlayActorSound` and `SoundVolume` (1.0 = the file), or `AmbientSound`, `LocalAmbientSound`,
  `ActivatorSound` and `ThingSound` (127 = the file, 254 = 2.0).
- Online play needs no protocol change. Servers have always sent a call's volume up to 2.0 (one byte,
  volume × 127), but clients capped it at 127 on receipt. They now pass it on and apply the SNDINFO
  setting, which they parse from the same wads, so a sound without `$fua_allow_amplify` still stops at
  1.0. An older client keeps capping at 1.0, and an older server sends the same bytes.
- An amplified sound can clip if its file already peaks near full scale.

## In-engine hooks

- `s_advsound.cpp`: the `$fua_allow_amplify` command, `CurrentAllowAmplify`, which is reset at each lump's
  start and end, and its stamping in `S_AddSoundLump`, `S_AddSound`'s redefinition and `$volume`.
- `s_sound.h`: `sfxinfo_t::bAllowAmplify`.
- `s_sound.cpp`: `S_StartSound` and `S_ChangeSoundVolume` take their limit from `ComputeSoundVolume`.
- `sv_commands.cpp` and `cl_main.cpp`: the `Sound`, `SoundActor`, `SoundActorIfNotPlaying`, `SoundSector`
  and `SoundPoint` commands write and read their volume byte through `ComputeSoundVolumeToWire` and
  `ComputeSoundVolumeFromWire`.
- `features/openal-sound/oalsound.cpp`: sources take `AL_MAX_GAIN` from `ComputeSourceMaxGain`, since it
  used to be the sound volume and capped every source at 1.0. Emscripten's OpenAL refuses a value above 1,
  and is asked once at startup; it applies `AL_GAIN` directly, so the browser amplifies too.

The pure part is `computation/sound_amplify_compute`, tested in `sound_amplify_compute_test.cpp`.

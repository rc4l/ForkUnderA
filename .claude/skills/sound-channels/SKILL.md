---
name: sound-channels
description: How to test ForkUnderA's sound channels end to end — overlapping sounds (CHAN_OVERLAP, CHANF_OVERLAP, CHAN_AUTO), slot replacement, CHAN_NOSTOP, looping flags, A_StopSound, and the server's looping record a late joiner hears — in single player and in a netgame, muted, with a pk3 of one emitter per rule and a checker that fails first on an old binary. Use for any change to s_sound.cpp's channel logic, A_PlaySound/A_StartSound/ACS PlaySound, the SoundActor commands, SERVER_UpdateLoopingChannels, or a report of sounds cutting each other off, stacking, going missing for a late joiner, or "more sounds at once than before".
---

# Testing sound channels

What `features/sound-overlap` (README there) promises, checked in a running engine. The rules are pure
and unit-tested (`sound_overlap_compute_test.cpp`, `soundactor_wire_nettest.cpp`); this is what only a
running engine shows: which sounds actually end up on which channel, on a client fed by the server, and
on a client that joined after the sounds started.

## Never play the test tones aloud

`make_pk3.py`'s emitters loop up to 31 tones at once, through the machine's real audio device — the user
hears it, and it is horrible. `check.mjs` launches every instance with `snd_sfxvolume 0` and
`snd_musicvolume 0` and refuses to spawn anything until the engine reports it muted. Keep it that way;
any other launch you make for sound work (`play`, `netplay`) gets `--cvar snd_sfxvolume=0` too unless the
user asked to listen. The checks read channels and flags, never levels, so muting costs nothing.

## Run it

```bash
S=.claude/skills/sound-channels
py -I $S/make_pk3.py <scratch>/channels.pk3                    # 14 emitters, 31 tone lumps
export FUACTL_ENGINE=<repo>/build-win/Release/forkundera.exe   # a bridge build (zandronum-rebuild)
IWAD=<repo>/tools/freedoom/freedoom2.wad
node $S/check.mjs <scratch>/channels.pk3 $IWAD                 # single player
node $S/check.mjs <scratch>/channels.pk3 $IWAD --net           # a player, then a late joiner
node $S/check.mjs <scratch>/channels.pk3 $IWAD --only FuaOvAuto,FuaOvNoStop
```

Each rule prints `PASS`/`FAIL` with what differed; exit 1 on any failure. Always pass the IWAD: a
`build-win` binary has no `iwads/` beside it, and the search found Steam's Strife, where a dedicated
server crashes at startup (a separate bug, `G_InitNew` → `SetLogText` with no console).

## The requirements, one emitter each

| Emitter | Requirement |
|---|---|
| `FuaOvAuto` | `CHAN_AUTO` overlaps on channel 0: ten loops on one actor all play (`0+`). The old engine capped an actor at 8 and dropped the rest. |
| `FuaOvReplace` | A plain sound replaces what its slot holds. |
| `FuaOvOverlap` | `CHAN_BODY\|CHAN_OVERLAP` sounds share the slot (`4+`). |
| `FuaOvReplaceAll` | A plain sound replaces **every** sound on the slot, overlapped ones too. |
| `FuaOvNoRestack` | The same overlapping loop started twice plays once. |
| `FuaOvOneShot` | An overlapping one-shot leaves the slot's loop playing. |
| `FuaOvCut` | A plain one-shot cuts the slot's loop. |
| `FuaOvNoStop` | `CHAN_NOSTOP` skips a sound the actor already plays on the slot. |
| `FuaOvNoStopOther` | `CHANF_NOSTOP` lets a *different* sound start (uzdoom's `S_PlaySoundPitch` checks the same sound, not "anything"). |
| `FuaOvLoopFlag` | `CHAN_LOOP` in A_PlaySound's slot loops the sound. |
| `FuaOvPickup` | `CHAN_PICKUP` is slot 3 with `CHAN_MAYBE_LOCAL` (flags `0x10`). |
| `FuaOvLooping` | `CHANF_LOOPING` loops once. |
| `FuaOvUI` | `CHANF_UI` reaches the sound (flags `0x20`); A_StartSound used to drop it. |
| `FuaOvStart` | `A_StartSound` + `CHANF_OVERLAP` shares the slot, and `A_StopSound(slot)` stops all of it. Judged on its own, before and after its stop 3 s in. |

A new channel rule gets an emitter in `make_pk3.py`'s `CASES` and a row in `check.mjs`'s `RULES`,
named for what it checks.

## Reading `listsoundchannels`

`fua/ovo1 at (x, y, z), channel 4+, flags 0x2101, volume 1.00, gain 1.1890` — the sound, its slot
(`+` = it overlaps), its `ChanFlags` in hex, its volume after the `$fua_allow_amplify` limit, and the
device gain (`snd_sfxvolume` × volume × the mono pan lift × rolloff; 0 when muted). Flag bits:
`1` 3D, `8` LISTENERZ, `0x10` MAYBE_LOCAL, `0x20` UI, `0x40` NOPAUSE, `0x80` AREA, `0x100` LOOP,
`0x200`/`0x400`/`0x800` internal, `0x1000` NOSTOP, `0x2000` OVERLAP.

## What bites

- **Sounds sharing a lump alias to one name.** `S_LoadSound` links a sound to the first loaded with the
  same lump, so `listsoundchannels` names them all after it and `$limit` (2 near copies) counts them
  together — an emitter "missing" is often the limit. One lump per sound, as `make_pk3.py` does.
- **One-shots last 1 s, and `FuaOvStart` stops at 3 s.** Eight `summon`s through the bridge take seconds;
  judge timed emitters alone.
- **The server plays no sounds.** Every "is it playing" check on a server is false, so `CHAN_NOSTOP` and
  the loop guards are decided on clients (`S_Sound` sends `SoundActorIfNotPlaying` for `CHAN_NOSTOP`).
- **A late joiner hears only loops**, resent from `g_LoopingSounds` (`sv_main.cpp`); its rules are the
  same as the player's minus one-shots. A late-joiner failure the player does not show is that record.
- **Only the channel short carries the flags.** `SoundActor`, `SoundActorIfNotPlaying` and `SoundSector`
  send the channel as a short; `Sound` and `SoundPoint` as a byte, which cannot carry `CHAN_NOSTOP` or
  `CHAN_OVERLAP`.

## Make it fail first

A pass proves something only if the old code fails it. Build the pk3 with numeric flags, which a binary
from before the DECORATE names still parses, and run it against that binary:

```bash
py -I $S/make_pk3.py <scratch>/channels_numeric.pk3 --numeric
FUACTL_ENGINE=<old>/forkundera.exe node $S/check.mjs <scratch>/channels_numeric.pk3 $IWAD --net
```

On the engine before `features/sound-overlap`, `FuaOvAuto` (8 of 10), `FuaOvOverlap`, `FuaOvOneShot`
and `FuaOvNoStop` fail for the player, and the late joiner gets a single `CHAN_AUTO` loop. An old binary's
`listsoundchannels` prints no channel or flags, so `check.mjs` judges only copies there: the rules that
are only about a channel or a flag (`FuaOvPickup`, `FuaOvUI`) pass vacuously, and need the new output.

## Beside this

- Unit tests: `features/sound-overlap/computation/sound_overlap_compute_test.cpp` (the rules and the
  late-join record), `soundactor_wire_nettest.cpp` (the command's bytes with the flags and an amplified
  volume), `features/sound-amplify` and `features/openal-sound/computation/oal_pan_compute_test.cpp` for
  levels.
- ACS `PlaySound` takes the same flags in its channel and reaches the same `S_Sound`; to drive it, build
  a BEHAVIOR lump with `tools/fuactl/src/acs.mjs` on a template map (`template-maps`).
- Wire changes go through `netcode-testing`; a change to what a flag value means, through `fork-exposure`.

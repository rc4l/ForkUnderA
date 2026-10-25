# Several sounds on one channel (`CHAN_OVERLAP`)

Ports UZDoom's overlapping sound channels. Before this, each actor had 8 channels holding one sound
each: a new sound on a channel stopped the old one, and `CHAN_AUTO` took a free channel and was
dropped when all 8 were busy.

- **`CHAN_AUTO` overlaps** (`uzdoom@4881ec257a`): it plays on channel 0 beside the actor's other
  auto sounds, with no limit per actor. `A_StopSound(CHAN_AUTO)` stops them all.
- **`CHAN_OVERLAP` (8192)** (`uzdoom@e82565373f`): added to a channel, the sound plays beside what
  that slot already holds instead of replacing it. It is a channel flag, like `CHAN_LOOP`, so it works
  in `A_PlaySound`'s slot (`CHAN_BODY | CHAN_OVERLAP`) and ACS `PlaySound`'s channel. `A_StartSound`'s
  `CHANF_OVERLAP` sets it on the slot asked for, so `A_StopSound(slot)` still finds the sound.
- A sound without it replaces **every** sound on its slot, overlapped ones included.
  `A_StopSound`, `SoundVolume` and the playing checks (`A_StartSound`'s `CHANF_NOSTOP`, loops not
  restacked) act on all of them.
- `snd_channels` defaults to 128 and never goes under 8 (`uzdoom@005e6871f9`, `uzdoom@2a489e7753`),
  since overlapping sounds use more voices at once. A config that saved 32 keeps it; Sound options →
  Sound channels changes it, after a sound restart.
- `compat_soundslots` (`COMPATF_MAGICSILENCE`) still moves every sound to `CHAN_WEAPON` first, so
  `CHAN_AUTO` cuts there as before; an explicit `CHAN_OVERLAP` still overlaps, as in UZDoom.

## Flags, at UZDoom's values

| DECORATE | Value | What it does here |
|---|---|---|
| `CHAN_LOOP` / `CHANF_LOOP` | 256 | loops the sound (`CHAN_LOOP` in A_PlaySound's slot, as before; now named) |
| `CHAN_NOSTOP` / `CHANF_NOSTOP` | 4096 | starts nothing while the actor already plays this sound on the slot, as uzdoom's `S_PlaySoundPitch` checks; a different sound still starts |
| `CHAN_OVERLAP` / `CHANF_OVERLAP` | 8192 | plays beside what the slot holds |
| `CHAN_PICKUP` | `CHAN_ITEM\|CHAN_MAYBE_LOCAL` | as in UZDoom |
| `CHANF_LISTENERZ`, `CHANF_MAYBE_LOCAL`, `CHANF_UI`, `CHANF_NOPAUSE` | 8, 16, 32, 64 | now reach the sound from A_StartSound, which dropped them |
| `CHANF_LOOPING` | `CHANF_LOOP\|CHANF_NOSTOP` | uzdoom's stand-in for the old looping argument |

`CHAN_NOSTOP` lives in `S_Sound(actor)`, so A_PlaySound, A_StartSound and ACS `PlaySound` share it.
Only clients hold the sounds, so online the server sends `SoundActorIfNotPlaying` and each client
decides.

Not ported: UZDoom's channel numbers past 7 (a slot is still masked to 0-7); `CHANF_LOCAL`, which needs
the server to send a sound to one player; `CHANF_TRANSIENT` and `CHANF_FORCE`, which do not fit the
channel short a server sends (32768 and up); `CHANF_SINGULAR`, which only a client could judge; and
`CHANF_RUMBLE`/`CHANF_NORUMBLE`, with no rumble here. They stay undefined, so a lump using one stops
with an unknown identifier instead of silently doing nothing.

## Online

The flag rides in the channel short of `SoundActor`, `SoundActorIfNotPlaying` and `SoundSector`, so
the protocol is unchanged; `Sound` and `SoundPoint` carry their channel in a byte and cannot carry it.
The server keeps one record per looping sound rather than per channel, so a late joiner hears every
overlapping loop, and an overlapping one-shot sound no longer drops the slot's loop from that record.

## In-engine hooks

- `s_sound.h`: `CHAN_NOSTOP`, `CHAN_OVERLAP`.
- `s_sound.cpp`: `S_StartSound` (CHAN_AUTO's flags, stopping every sound on the slot), `S_Sound(actor)`
  (`CHAN_NOSTOP`), `S_IsActorPlayingSomething` and `S_ChangeSoundVolume` over every sound;
  `snd_channels`; `listsoundchannels` prints each sound's channel and flags.
- `thingdef/thingdef_codeptr.cpp`: `A_StartSound` builds its channel with `ComputeStartSoundChannel`;
  `A_PlaySoundEx` records its one-shot sounds on the slot instead of the channel name's index.
- `cl_main.cpp`: `SoundActorIfNotPlaying` checks the slot alone.
- `sv_main.cpp`: the looping-sound record (`SERVER_UpdateLoopingChannels` and friends).
- `wadsrc/static/actors/constants.txt`: the DECORATE flags above.

The pure parts are `computation/sound_overlap_compute`; `soundactor_wire_nettest.cpp` pins the
command's bytes with the flags and an amplified volume. The `sound-channels` skill runs every rule in a
live engine, single player and with a late joiner.

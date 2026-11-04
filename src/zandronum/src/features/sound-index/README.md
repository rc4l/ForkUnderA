# Sound channels by sound and by actor

Every channel walk used to visit the whole channel list, which holds several hundred channels during
a crowded tic (each start a tic queues waits there for the handoff). Two indexes keep each channel
also in its sound's list and its actor's list:

- **By sound**, in the channel list's own order: the near limit (`S_CheckSoundLimit`) counts only
  that sound's channels and stops where the walk over every channel would have.
- **By actor**: `S_RelinkSound` (each actor destroyed), the slot a starting sound replaces,
  `S_StopSound`, `S_StopAllSoundsFromActor`, `S_GetSoundPlayingInfo` and `S_IsActorPlayingSomething`
  visit only that actor's channels. A NULL actor still walks every channel, for the forgotten ones.

`computation/soundindex_compute.*`: the intrusive lists and their heads.

Hooks in `s_sound.cpp`, wherever a channel's sound or actor is set or it leaves the list:
`S_StartSound` (set up), `S_ReturnChannel`, `S_StopChannel` (actor forgotten), `S_RelinkSound`,
`S_SerializeSounds` (loaded), `S_RunDeferredSounds` (the pass's unlink and relink) and
`S_Shutdown`. The links live in `FSoundChan` (`s_sound.h`).

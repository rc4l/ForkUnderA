// [rc4l] Pure sound-backend selection, extracted from I_InitSound so it can be unit-tested
// without linking the engine. Build-time and runtime facts (whether OpenAL is compiled in,
// whether its library is present) are passed in as args so every combination is coverable.
// Implementation in sound_backend_compute.cpp.
#ifndef ZX_SOUND_BACKEND_COMPUTE_H
#define ZX_SOUND_BACKEND_COMPUTE_H

// [rc4l] Which renderer I_InitSound should construct.
enum
{
	// [rc4l] NullSoundRenderer: silence, but a valid renderer.
	ZX_SNDBACKEND_NULL = 0,
	// [rc4l] OpenALSoundRenderer.
	ZX_SNDBACKEND_OPENAL = 1,
	// [rc4l] The requested backend is known but unavailable, so construct nothing.
	ZX_SNDBACKEND_UNAVAILABLE = 2,
	// [rc4l] The name is not a backend we know; the caller reports it to the console.
	ZX_SNDBACKEND_UNKNOWN = 3,
};

// [rc4l] The decision, plus whether the snd_backend cvar should be rewritten to match.
struct SoundBackendChoice
{
	int backend;
	bool renameToOpenAL;
};

// [rc4l] Resolve which sound renderer to build. requested is the snd_backend cvar (may be
// null); noSound covers -nosound/-host; openalCompiledIn is !NO_OPENAL; openalPresent is
// whether the OpenAL library actually loaded. "fmod" is accepted as a legacy alias because
// FMOD was removed from the tree and old configs still name it.
SoundBackendChoice ComputeSoundBackendChoice(const char *requested, bool noSound,
	bool openalCompiledIn, bool openalPresent);

// [rc4l] Sentinel for "no sound renderer built yet" -- the value the engine seeds its
// active-backend tracker with before I_InitSound has ever run.
enum { ZX_SNDBACKEND_NONE = -1 };

// [rc4l] Whether the snd_backend cvar callback must tear down and rebuild the renderer.
// The callback used to unconditionally I_CloseSound()+I_InitSound(); at startup that fired
// once from EnableCallbacks right after I_Init had ALREADY built the right renderer, so the
// engine opened the sound device, closed it, and reopened it -- the doubled "Initializing
// OpenAL" log. This returns false when the already-active backend already matches what the
// cvar now resolves to (so the callback skips the redundant rebuild -- the startup pass and,
// importantly, a wad_reload restart, where the backend is unchanged and sound must persist),
// and true only when the resolved backend actually differs or nothing is active yet. The
// resolved choice is normalized to what I_InitSound actually CONSTRUCTS: OpenAL only for the
// OpenAL choice; NULL/UNAVAILABLE/UNKNOWN all end up as the null renderer.
bool SoundBackendReinitNeeded(int activeBackend, const char *requested, bool noSound,
	bool openalCompiledIn, bool openalPresent);

#endif // ZX_SOUND_BACKEND_COMPUTE_H

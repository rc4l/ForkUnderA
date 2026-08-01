// [rc4l] Implementation of the pure sound-backend selection. No engine dependencies, so
// both the engine and the standalone test build compile this TU.
#include "sound/computation/sound_backend_compute.h"

#include <cctype>

namespace
{
// [rc4l] Local case-insensitive compare so this stays free of the engine's stricmp. Only
// the cvar side can be null; the literal side is always supplied by this file, so testing
// it would add a branch no caller can reach.
bool EqualsIgnoreCase(const char *a, const char *literal)
{
	if (a == nullptr)
		return false;

	const char *b = literal;
	while (*a != '\0' && *b != '\0')
	{
		const unsigned char ca = static_cast<unsigned char>(*a);
		const unsigned char cb = static_cast<unsigned char>(*b);
		if (std::tolower(ca) != std::tolower(cb))
			return false;
		++a;
		++b;
	}
	return *a == *b;
}

// [rc4l] OpenAL is usable only when it is both compiled in and actually loadable.
SoundBackendChoice ChooseOpenAL(bool openalCompiledIn, bool openalPresent, bool rename)
{
	SoundBackendChoice choice;
	if (openalCompiledIn && openalPresent)
	{
		choice.backend = ZX_SNDBACKEND_OPENAL;
		choice.renameToOpenAL = rename;
	}
	else
	{
		choice.backend = ZX_SNDBACKEND_UNAVAILABLE;
		choice.renameToOpenAL = false;
	}
	return choice;
}
} // namespace

SoundBackendChoice ComputeSoundBackendChoice(const char *requested, bool noSound,
	bool openalCompiledIn, bool openalPresent)
{
	SoundBackendChoice choice;
	choice.renameToOpenAL = false;

	// -nosound/-host wins over whatever the cvar says.
	if (noSound)
	{
		choice.backend = ZX_SNDBACKEND_NULL;
		return choice;
	}

	if (EqualsIgnoreCase(requested, "null"))
	{
		choice.backend = ZX_SNDBACKEND_NULL;
		return choice;
	}

	// FMOD is gone, so a config still naming it is steered to OpenAL and rewritten, which
	// stops the migration message reappearing on every launch.
	if (EqualsIgnoreCase(requested, "fmod"))
		return ChooseOpenAL(openalCompiledIn, openalPresent, true);

	if (EqualsIgnoreCase(requested, "openal"))
		return ChooseOpenAL(openalCompiledIn, openalPresent, false);

	choice.backend = ZX_SNDBACKEND_UNKNOWN;
	return choice;
}

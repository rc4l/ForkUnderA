// [rc4l] Covers every branch of the sound-backend selection, including the legacy "fmod"
// alias that exists because FMOD was removed from the tree.
#include <gtest/gtest.h>

#include "sound/computation/sound_backend_compute.h"

namespace
{
// [rc4l] The common case: OpenAL both compiled in and loadable.
SoundBackendChoice Choose(const char *requested, bool noSound = false)
{
	return ComputeSoundBackendChoice(requested, noSound, /*openalCompiledIn=*/true,
		/*openalPresent=*/true);
}
} // namespace

// [rc4l] The happy path a normal launch actually takes: sound on, default cvar, OpenAL
// available -- and the resulting backend is the one the engine constructs.
TEST(SoundBackendCompute, HappyPathDefaultLaunchSelectsOpenAL)
{
	const SoundBackendChoice choice = Choose("openal");
	EXPECT_EQ(choice.backend, ZX_SNDBACKEND_OPENAL);
	EXPECT_FALSE(choice.renameToOpenAL);
}

TEST(SoundBackendCompute, NoSoundBeatsEveryCvarValue)
{
	// [rc4l] -nosound/-host must win even when a real backend is requested and available.
	EXPECT_EQ(Choose("openal", /*noSound=*/true).backend, ZX_SNDBACKEND_NULL);
	EXPECT_EQ(Choose("fmod", /*noSound=*/true).backend, ZX_SNDBACKEND_NULL);
	EXPECT_EQ(Choose("null", /*noSound=*/true).backend, ZX_SNDBACKEND_NULL);
	EXPECT_EQ(Choose("nonsense", /*noSound=*/true).backend, ZX_SNDBACKEND_NULL);
	EXPECT_FALSE(Choose("openal", /*noSound=*/true).renameToOpenAL);
}

TEST(SoundBackendCompute, NullBackendIsHonoured)
{
	EXPECT_EQ(Choose("null").backend, ZX_SNDBACKEND_NULL);
	EXPECT_FALSE(Choose("null").renameToOpenAL);
}

TEST(SoundBackendCompute, LegacyFmodConfigMigratesToOpenAL)
{
	// [rc4l] An old config naming FMOD gets OpenAL, and asks the caller to rewrite the cvar.
	const SoundBackendChoice choice = Choose("fmod");
	EXPECT_EQ(choice.backend, ZX_SNDBACKEND_OPENAL);
	EXPECT_TRUE(choice.renameToOpenAL);
}

TEST(SoundBackendCompute, BackendNamesAreCaseInsensitive)
{
	EXPECT_EQ(Choose("OpenAL").backend, ZX_SNDBACKEND_OPENAL);
	EXPECT_EQ(Choose("NULL").backend, ZX_SNDBACKEND_NULL);
	EXPECT_TRUE(Choose("FMod").renameToOpenAL);
}

TEST(SoundBackendCompute, UnavailableOpenALYieldsUnavailableNotUnknown)
{
	// [rc4l] Compiled out, and present-but-not-compiled-in, and compiled-in-but-absent.
	EXPECT_EQ(ComputeSoundBackendChoice("openal", false, /*compiledIn=*/false,
		/*present=*/true).backend, ZX_SNDBACKEND_UNAVAILABLE);
	EXPECT_EQ(ComputeSoundBackendChoice("openal", false, /*compiledIn=*/true,
		/*present=*/false).backend, ZX_SNDBACKEND_UNAVAILABLE);
	EXPECT_EQ(ComputeSoundBackendChoice("fmod", false, /*compiledIn=*/false,
		/*present=*/false).backend, ZX_SNDBACKEND_UNAVAILABLE);
	// [rc4l] An unavailable backend must never ask for the cvar rewrite.
	EXPECT_FALSE(ComputeSoundBackendChoice("fmod", false, false, false).renameToOpenAL);
}

TEST(SoundBackendCompute, UnrecognisedNamesReportUnknown)
{
	EXPECT_EQ(Choose("wasapi").backend, ZX_SNDBACKEND_UNKNOWN);
	EXPECT_EQ(Choose("").backend, ZX_SNDBACKEND_UNKNOWN);
	// [rc4l] A prefix of a real name is not a match.
	EXPECT_EQ(Choose("open").backend, ZX_SNDBACKEND_UNKNOWN);
	// [rc4l] Nor is a name that merely starts with one.
	EXPECT_EQ(Choose("openalx").backend, ZX_SNDBACKEND_UNKNOWN);
}

TEST(SoundBackendCompute, NullCvarPointerIsTreatedAsUnknown)
{
	// [rc4l] snd_backend can be read before the cvar is initialised; must not dereference.
	EXPECT_EQ(Choose(nullptr).backend, ZX_SNDBACKEND_UNKNOWN);
	// [rc4l] ...but -nosound still short-circuits before the name is examined.
	EXPECT_EQ(Choose(nullptr, /*noSound=*/true).backend, ZX_SNDBACKEND_NULL);
}

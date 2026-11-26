#pragma once

#include <cstddef>
#include <cstdint>

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] One bit's move between two layouts of a flag word, to 0 when it leaves the word.
struct BitMove
{
	uint32_t from;
	uint32_t to;
};

// [Doruk-Aksoy] `value` with each move's from bit at its to bit, bits no move names kept where they are.
uint32_t ComputeMovedBits(uint32_t value, const BitMove *moves, size_t count);

// [Doruk-Aksoy] Saves before this version hold flags7, flags8 and flags9 in our layout before they took UZDoom's.
const int kFlagRehomeSaveVer = 4538;

// [Doruk-Aksoy] flags7's NODECAL, FORCEDECAL and FLYCHEAT, from Zandronum's bits to UZDoom's.
constexpr BitMove kFlags7Moves[] = {
	{ 0x00020000, 0x00040000 },	// NODECAL
	{ 0x00040000, 0x00080000 },	// FORCEDECAL
	{ 0x00080000, 0x00020000 },	// FLYCHEAT
};

// [Doruk-Aksoy] flags8's MBF21 flags, from Zandronum lz/mbf21's bits to UZDoom's, FULLVOLDEATH leaving for flags3.
constexpr BitMove kFlags8Moves[] = {
	{ 0x00000001, 0 },			// FULLVOLDEATH
	{ 0x00000002, 0x00010000 },	// FULLVOLSEE
	{ 0x00000004, 0x00020000 },	// E1M8BOSS
	{ 0x00000008, 0x00040000 },	// E2M8BOSS
	{ 0x00000010, 0x00080000 },	// E3M8BOSS
	{ 0x00000020, 0x00200000 },	// E4M6BOSS
	{ 0x00000040, 0x00100000 },	// E4M8BOSS
	{ 0x00000080, 0x00400000 },	// MAP07BOSS1
	{ 0x00000100, 0x00800000 },	// MAP07BOSS2
};

// [Doruk-Aksoy] The old flags8 FULLVOLDEATH bit, and flags3's FULLVOLDEATH that UZDoom keeps it in.
constexpr uint32_t kOldFlags8FullVolDeath = 0x00000001;
constexpr uint32_t kFlags3FullVolDeath = 0x00400000;

// [Doruk-Aksoy] An old save's flags3, flags7 and flags8, in today's layout.
struct SavedFlags
{
	uint32_t flags3;
	uint32_t flags7;
	uint32_t flags8;
};

// [Doruk-Aksoy] `saved` as it reads in today's layout.
SavedFlags ComputeRehomedSaveFlags(SavedFlags saved);

// [Doruk-Aksoy] Saves before this version hold LIGHTMULTALPHA in renderflags and no renderflags2.
const int kRenderRehomeSaveVer = 4539;

// [Doruk-Aksoy] Our LIGHTMULTALPHA's old renderflags bit, and UZDoom's in renderflags2.
constexpr uint32_t kOldRenderLightMultAlpha = 0x00080000;
constexpr uint32_t kRenderFlags2LightMultAlpha = 0x00000400;

// [Doruk-Aksoy] An old save's renderflags split into today's two words.
struct SavedRenderFlags
{
	uint32_t renderflags;
	uint32_t renderflags2;
};

// [Doruk-Aksoy] `renderflags` from a save before kRenderRehomeSaveVer, as today's two words.
SavedRenderFlags ComputeRehomedSaveRenderFlags(uint32_t renderflags);

} }

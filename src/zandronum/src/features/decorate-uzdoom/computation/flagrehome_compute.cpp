#include "flagrehome_compute.h"

namespace zx { namespace decorateuzdoom {

uint32_t ComputeMovedBits(uint32_t value, const BitMove *moves, size_t count)
{
	uint32_t from = 0, to = 0;
	for (size_t i = 0; i < count; i++)
	{
		from |= moves[i].from;
		if (value & moves[i].from) to |= moves[i].to;
	}
	return (value & ~from) | to;
}

SavedFlags ComputeRehomedSaveFlags(SavedFlags saved)
{
	SavedFlags out;
	out.flags3 = saved.flags3 | ((saved.flags8 & kOldFlags8FullVolDeath) ? kFlags3FullVolDeath : 0);
	out.flags7 = ComputeMovedBits(saved.flags7, kFlags7Moves, sizeof(kFlags7Moves) / sizeof(kFlags7Moves[0]));
	out.flags8 = ComputeMovedBits(saved.flags8, kFlags8Moves, sizeof(kFlags8Moves) / sizeof(kFlags8Moves[0]));
	return out;
}

} }

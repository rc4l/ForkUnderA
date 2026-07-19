// [rc4l] Implementation of the voice-chat float<->bytes conversions. No engine deps, so
// both the engine and the standalone test build compile this TU.
#include "computation/voicechat_convert_compute.h"
#include <cstdint>

float ComputeBytesToFloat(const unsigned char *bytes)
{
	if (bytes == nullptr)
		return 0.0f;

	union { uint32_t l; float f; } dataUnion;
	dataUnion.l = 0;

	for (unsigned int i = 0; i < 4; i++)
		dataUnion.l |= static_cast<uint32_t>(bytes[i]) << (8 * i);

	return dataUnion.f;
}

void ComputeFloatToBytes(float value, unsigned char *bytes)
{
	if (bytes == nullptr)
		return;

	union { uint32_t l; float f; } dataUnion;
	dataUnion.f = value;

	for (unsigned int i = 0; i < 4; i++)
		bytes[i] = (dataUnion.l >> (8 * i)) & 0xFF;
}

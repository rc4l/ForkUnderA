// [rc4l] Pure little-endian float <-> 4-byte serialization used by the voice-chat audio
// path (FMOD playback + OpenAL capture). Extracted so it's unit-testable without the
// engine. Implementation in voicechat_convert_compute.cpp.
#ifndef ZX_VOICECHAT_CONVERT_COMPUTE_H
#define ZX_VOICECHAT_CONVERT_COMPUTE_H

// [rc4l] Deserialize 4 little-endian bytes into a float; returns 0 for a null pointer.
float ComputeBytesToFloat(const unsigned char *bytes);

// [rc4l] Serialize a float into 4 little-endian bytes; a null destination is a no-op.
void ComputeFloatToBytes(float value, unsigned char *bytes);

#endif // ZX_VOICECHAT_CONVERT_COMPUTE_H

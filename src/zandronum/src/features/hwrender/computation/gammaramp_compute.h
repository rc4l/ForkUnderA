// [rc4l] Pure 256-entry 16-bit gamma ramp math for the OS gamma APIs, including the identity ramp used to restore display gamma on exit.
#ifndef ZX_HWRENDER_GAMMARAMP_COMPUTE_H
#define ZX_HWRENDER_GAMMARAMP_COMPUTE_H

namespace hwrender
{

// [rc4l] Fill a 256-entry 16-bit ramp for the given gamma (>1 brightens). A non-positive gamma is treated as the identity ramp rather than dividing by zero.
void ComputeGammaRamp(float gamma, unsigned short out[256]);

// [rc4l] Fill the linear/identity ramp (out[i] = i*257, so 0->0 and 255->65535). Used to restore the display's gamma on shutdown/crash so an abrupt exit doesn't leave the desktop washed out.
void ComputeIdentityRamp(unsigned short out[256]);

} // namespace hwrender

#endif // ZX_HWRENDER_GAMMARAMP_COMPUTE_H

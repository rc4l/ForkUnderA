// [rc4l] Clean-room fixed-point scale math for ZandroX.
//
// Independently authored from the standard idiom for fixed-point multiply/divide
// (a 64-bit intermediate shifted back down), NOT derived from Ken Silverman's
// Build "pragmas.h" — so ZandroX carries no BUILD-licensed code here. Standalone
// (only <cstdint>) so it is unit-testable without the engine.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_FIXEDMATH_H
#define ZX_FIXEDMATH_H

#include <cstdint>

// Generic forms: shift is a runtime argument.
inline int32_t Scale(int32_t a, int32_t b, int32_t c)                       { return int32_t(((int64_t)a * b) / c); }
inline int32_t MulScale(int32_t a, int32_t b, int32_t s)                    { return int32_t(((int64_t)a * b) >> s); }
inline int32_t DivScale(int32_t a, int32_t b, int32_t s)                    { return int32_t(((int64_t)a << s) / b); }
inline int32_t DMulScale(int32_t a, int32_t b, int32_t c, int32_t d, int32_t s) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> s); }
inline uint32_t UMulScale16(uint32_t a, uint32_t b)                         { return uint32_t(((uint64_t)a * b) >> 16); }
inline int ksgn(int a)                                                      { return (a > 0) - (a < 0); }

// Bounded multiply-scale: clamp the 64-bit result into int32 range.
inline int32_t BoundMulScale(int32_t a, int32_t b, int32_t s)
{
	int64_t t = ((int64_t)a * b) >> s;
	if (t < INT32_MIN) return INT32_MIN;
	if (t > INT32_MAX) return INT32_MAX;
	return (int32_t)t;
}

inline void clearbuf(void *d, int c, int32_t a)      { int32_t *p = (int32_t *)d; while (c-- > 0) *p++ = a; }
inline void clearbufshort(void *d, int c, uint16_t a){ uint16_t *p = (uint16_t *)d; while (c-- > 0) *p++ = a; }

inline int32_t MulScale1(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 1); }
inline int32_t DivScale1(int32_t a, int32_t b) { return int32_t(((int64_t)a << 1) / b); }
inline int32_t DMulScale1(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 1); }
inline int32_t TMulScale1(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 1); }
inline int32_t MulScale2(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 2); }
inline int32_t DivScale2(int32_t a, int32_t b) { return int32_t(((int64_t)a << 2) / b); }
inline int32_t DMulScale2(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 2); }
inline int32_t TMulScale2(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 2); }
inline int32_t MulScale3(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 3); }
inline int32_t DivScale3(int32_t a, int32_t b) { return int32_t(((int64_t)a << 3) / b); }
inline int32_t DMulScale3(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 3); }
inline int32_t TMulScale3(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 3); }
inline int32_t MulScale4(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 4); }
inline int32_t DivScale4(int32_t a, int32_t b) { return int32_t(((int64_t)a << 4) / b); }
inline int32_t DMulScale4(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 4); }
inline int32_t TMulScale4(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 4); }
inline int32_t MulScale5(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 5); }
inline int32_t DivScale5(int32_t a, int32_t b) { return int32_t(((int64_t)a << 5) / b); }
inline int32_t DMulScale5(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 5); }
inline int32_t TMulScale5(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 5); }
inline int32_t MulScale6(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 6); }
inline int32_t DivScale6(int32_t a, int32_t b) { return int32_t(((int64_t)a << 6) / b); }
inline int32_t DMulScale6(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 6); }
inline int32_t TMulScale6(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 6); }
inline int32_t MulScale7(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 7); }
inline int32_t DivScale7(int32_t a, int32_t b) { return int32_t(((int64_t)a << 7) / b); }
inline int32_t DMulScale7(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 7); }
inline int32_t TMulScale7(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 7); }
inline int32_t MulScale8(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 8); }
inline int32_t DivScale8(int32_t a, int32_t b) { return int32_t(((int64_t)a << 8) / b); }
inline int32_t DMulScale8(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 8); }
inline int32_t TMulScale8(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 8); }
inline int32_t MulScale9(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 9); }
inline int32_t DivScale9(int32_t a, int32_t b) { return int32_t(((int64_t)a << 9) / b); }
inline int32_t DMulScale9(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 9); }
inline int32_t TMulScale9(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 9); }
inline int32_t MulScale10(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 10); }
inline int32_t DivScale10(int32_t a, int32_t b) { return int32_t(((int64_t)a << 10) / b); }
inline int32_t DMulScale10(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 10); }
inline int32_t TMulScale10(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 10); }
inline int32_t MulScale11(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 11); }
inline int32_t DivScale11(int32_t a, int32_t b) { return int32_t(((int64_t)a << 11) / b); }
inline int32_t DMulScale11(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 11); }
inline int32_t TMulScale11(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 11); }
inline int32_t MulScale12(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 12); }
inline int32_t DivScale12(int32_t a, int32_t b) { return int32_t(((int64_t)a << 12) / b); }
inline int32_t DMulScale12(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 12); }
inline int32_t TMulScale12(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 12); }
inline int32_t MulScale13(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 13); }
inline int32_t DivScale13(int32_t a, int32_t b) { return int32_t(((int64_t)a << 13) / b); }
inline int32_t DMulScale13(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 13); }
inline int32_t TMulScale13(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 13); }
inline int32_t MulScale14(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 14); }
inline int32_t DivScale14(int32_t a, int32_t b) { return int32_t(((int64_t)a << 14) / b); }
inline int32_t DMulScale14(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 14); }
inline int32_t TMulScale14(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 14); }
inline int32_t MulScale15(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 15); }
inline int32_t DivScale15(int32_t a, int32_t b) { return int32_t(((int64_t)a << 15) / b); }
inline int32_t DMulScale15(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 15); }
inline int32_t TMulScale15(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 15); }
inline int32_t MulScale16(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 16); }
inline int32_t DivScale16(int32_t a, int32_t b) { return int32_t(((int64_t)a << 16) / b); }
inline int32_t DMulScale16(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 16); }
inline int32_t TMulScale16(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 16); }
inline int32_t MulScale17(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 17); }
inline int32_t DivScale17(int32_t a, int32_t b) { return int32_t(((int64_t)a << 17) / b); }
inline int32_t DMulScale17(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 17); }
inline int32_t TMulScale17(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 17); }
inline int32_t MulScale18(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 18); }
inline int32_t DivScale18(int32_t a, int32_t b) { return int32_t(((int64_t)a << 18) / b); }
inline int32_t DMulScale18(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 18); }
inline int32_t TMulScale18(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 18); }
inline int32_t MulScale19(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 19); }
inline int32_t DivScale19(int32_t a, int32_t b) { return int32_t(((int64_t)a << 19) / b); }
inline int32_t DMulScale19(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 19); }
inline int32_t TMulScale19(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 19); }
inline int32_t MulScale20(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 20); }
inline int32_t DivScale20(int32_t a, int32_t b) { return int32_t(((int64_t)a << 20) / b); }
inline int32_t DMulScale20(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 20); }
inline int32_t TMulScale20(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 20); }
inline int32_t MulScale21(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 21); }
inline int32_t DivScale21(int32_t a, int32_t b) { return int32_t(((int64_t)a << 21) / b); }
inline int32_t DMulScale21(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 21); }
inline int32_t TMulScale21(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 21); }
inline int32_t MulScale22(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 22); }
inline int32_t DivScale22(int32_t a, int32_t b) { return int32_t(((int64_t)a << 22) / b); }
inline int32_t DMulScale22(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 22); }
inline int32_t TMulScale22(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 22); }
inline int32_t MulScale23(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 23); }
inline int32_t DivScale23(int32_t a, int32_t b) { return int32_t(((int64_t)a << 23) / b); }
inline int32_t DMulScale23(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 23); }
inline int32_t TMulScale23(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 23); }
inline int32_t MulScale24(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 24); }
inline int32_t DivScale24(int32_t a, int32_t b) { return int32_t(((int64_t)a << 24) / b); }
inline int32_t DMulScale24(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 24); }
inline int32_t TMulScale24(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 24); }
inline int32_t MulScale25(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 25); }
inline int32_t DivScale25(int32_t a, int32_t b) { return int32_t(((int64_t)a << 25) / b); }
inline int32_t DMulScale25(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 25); }
inline int32_t TMulScale25(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 25); }
inline int32_t MulScale26(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 26); }
inline int32_t DivScale26(int32_t a, int32_t b) { return int32_t(((int64_t)a << 26) / b); }
inline int32_t DMulScale26(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 26); }
inline int32_t TMulScale26(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 26); }
inline int32_t MulScale27(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 27); }
inline int32_t DivScale27(int32_t a, int32_t b) { return int32_t(((int64_t)a << 27) / b); }
inline int32_t DMulScale27(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 27); }
inline int32_t TMulScale27(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 27); }
inline int32_t MulScale28(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 28); }
inline int32_t DivScale28(int32_t a, int32_t b) { return int32_t(((int64_t)a << 28) / b); }
inline int32_t DMulScale28(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 28); }
inline int32_t TMulScale28(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 28); }
inline int32_t MulScale29(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 29); }
inline int32_t DivScale29(int32_t a, int32_t b) { return int32_t(((int64_t)a << 29) / b); }
inline int32_t DMulScale29(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 29); }
inline int32_t TMulScale29(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 29); }
inline int32_t MulScale30(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 30); }
inline int32_t DivScale30(int32_t a, int32_t b) { return int32_t(((int64_t)a << 30) / b); }
inline int32_t DMulScale30(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 30); }
inline int32_t TMulScale30(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 30); }
inline int32_t MulScale31(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 31); }
inline int32_t DivScale31(int32_t a, int32_t b) { return int32_t(((int64_t)a << 31) / b); }
inline int32_t DMulScale31(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 31); }
inline int32_t TMulScale31(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 31); }
inline int32_t MulScale32(int32_t a, int32_t b) { return int32_t(((int64_t)a * b) >> 32); }
inline int32_t DivScale32(int32_t a, int32_t b) { return int32_t(((int64_t)a << 32) / b); }
inline int32_t DMulScale32(int32_t a, int32_t b, int32_t c, int32_t d) { return int32_t((((int64_t)a * b) + ((int64_t)c * d)) >> 32); }
inline int32_t TMulScale32(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f) { return int32_t((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 32); }

#endif // ZX_FIXEDMATH_H

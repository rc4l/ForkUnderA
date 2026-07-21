// [rc4l] Strong 16.16 fixed-point type for the "types not tests" migration.
//
// Wraps an int64 with faithful arithmetic (zero runtime cost after inlining) but REJECTS at
// compile time the two implicit conversions that produced the fixed_t-widening bugs:
//   * unsigned / angle_t  ->  fixed   : a uint32 angle or wrapping delta zero-extends instead of
//                                       reinterpreting as a signed delta (the "+right spins" bug).
//   * fixed  ->  int (or narrower)     : a 64-bit distance/coord silently truncates to 32 bits
//                                       (the pusher / seeker distance bugs).
// Both must now be written explicitly (FromRaw / .Raw() / a named cast), which forces a human to
// look at exactly the boundary where those bugs live -- including in future UZDoom/Zandronum
// backports.
//
// Design notes:
//   * Construction from a SIGNED integer is implicit (sign-extends safely) so `fixed_t x = 0;`,
//     `x < 0`, `x + FRACUNIT`, `3*FRACUNIT` keep working and comparisons need no extra overloads.
//   * Construction from UNSIGNED is deleted -> angle_t/DWORD sources are a compile error.
//   * Conversion TO integer is `explicit` -> `int i = fixedExpr;` is a compile error.
//   * fixed*int / fixed>>n (scaling by a count) are provided; fixed*fixed is intentionally NOT --
//     callers must use FixedMul, so a bare fixed*fixed overflow can't recur.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_FIXED_STRONG_H
#define ZX_FIXED_STRONG_H

#include <cstdint>
#include <type_traits>

namespace zx
{

class Fixed
{
public:
	// [rc4l] Trivial default ctor (leaves v_ uninitialized, exactly like a raw `fixed_t x;`), so
	// Fixed stays trivially-default-constructible and can be a union member (e.g. FMetaData) and
	// live in memcpy'd/aggregate storage. Value-init (`Fixed x{}` / `= {}`) still zeroes.
	Fixed() = default;

	// [rc4l] Signed integer sources sign-extend safely -> implicit is fine and keeps literals,
	// comparisons, and `+`/`-` with plain ints working without a flood of overloads.
	constexpr Fixed(int v) : v_(v) {}
	constexpr Fixed(long v) : v_(static_cast<int64_t>(v)) {}
	constexpr Fixed(long long v) : v_(static_cast<int64_t>(v)) {}

	// [rc4l] Unsigned sources are the hazard (zero-extension of a value that meant a signed
	// delta). Making them EXPLICIT blocks the silent path -- `fixed_t f = someAngle;` and passing
	// an angle_t to a fixed parameter still fail -- while letting an author who genuinely means it
	// write `(fixed_t)x` / `Fixed(x)`. The explicit cast is the visible "I take responsibility"
	// escape hatch; the implicit conversion that caused the bugs stays forbidden.
	explicit constexpr Fixed(unsigned v) : v_(static_cast<int64_t>(v)) {}
	explicit constexpr Fixed(unsigned long v) : v_(static_cast<int64_t>(v)) {}
	explicit constexpr Fixed(unsigned long long v) : v_(static_cast<int64_t>(v)) {}

	// [rc4l] The blessed, visible ways to cross the boundary on purpose.
	constexpr int64_t Raw() const { return v_; }
	static constexpr Fixed FromRaw(int64_t v) { return Fixed(v); }

	// [rc4l] Narrowing / lossy conversions out of fixed are explicit only.
	explicit constexpr operator int() const { return static_cast<int>(v_); }
	explicit constexpr operator int64_t() const { return v_; }
	explicit constexpr operator unsigned() const { return static_cast<unsigned>(v_); }
	explicit constexpr operator short() const { return static_cast<short>(v_); }
	explicit constexpr operator float() const { return static_cast<float>(v_); }
	explicit constexpr operator double() const { return static_cast<double>(v_); }
	explicit constexpr operator bool() const { return v_ != 0; }

	// --- additive: fixed +/- fixed (ints promote via the implicit ctor) ---
	friend constexpr Fixed operator+(Fixed a, Fixed b) { return FromRaw(a.v_ + b.v_); }
	friend constexpr Fixed operator-(Fixed a, Fixed b) { return FromRaw(a.v_ - b.v_); }
	constexpr Fixed operator-() const { return FromRaw(-v_); }
	constexpr Fixed operator+() const { return *this; }

	// --- scaling by an integer count (NOT fixed*fixed -- that must go through FixedMul) ---
	friend constexpr Fixed operator*(Fixed a, int b) { return FromRaw(a.v_ * b); }
	friend constexpr Fixed operator*(int a, Fixed b) { return FromRaw(a * b.v_); }
	friend constexpr Fixed operator/(Fixed a, int b) { return FromRaw(a.v_ / b); }
	friend constexpr Fixed operator%(Fixed a, int b) { return FromRaw(a.v_ % b); }

	// --- bit ops (shifts and masks are used pervasively on fixed values) ---
	friend constexpr Fixed operator<<(Fixed a, int n) { return FromRaw(a.v_ << n); }
	friend constexpr Fixed operator>>(Fixed a, int n) { return FromRaw(a.v_ >> n); }

	// [rc4l] Bitwise masks. A mask of 32 bits or fewer is reinterpreted as a 32-bit pattern and
	// SIGN-EXTENDED to 64 bits -- the author's 32-bit mental model. So a high-bit "clear the low N
	// bits" mask like 0xFFFFFE00 stays sign-preserving on the widened value instead of wiping the
	// sign (the polyobject-rotation bug), while low-bit masks (& 0xFFFF, & FINEMASK) are unchanged
	// because sign-extending a positive mask is a no-op. Genuine 64-bit masks pass through as-is.
	// This makes that whole bug class impossible -- including in backported code.
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	static constexpr int64_t widenMask(M m) { return sizeof(M) <= 4 ? int64_t(int32_t(m)) : int64_t(m); }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator&(Fixed a, M m) { return FromRaw(a.v_ & widenMask(m)); }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator|(Fixed a, M m) { return FromRaw(a.v_ | widenMask(m)); }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator^(Fixed a, M m) { return FromRaw(a.v_ ^ widenMask(m)); }
	constexpr Fixed operator~() const { return FromRaw(~v_); }

	// --- comparisons (ints promote via the implicit ctor) ---
	friend constexpr bool operator==(Fixed a, Fixed b) { return a.v_ == b.v_; }
	friend constexpr bool operator!=(Fixed a, Fixed b) { return a.v_ != b.v_; }
	friend constexpr bool operator<(Fixed a, Fixed b) { return a.v_ < b.v_; }
	friend constexpr bool operator<=(Fixed a, Fixed b) { return a.v_ <= b.v_; }
	friend constexpr bool operator>(Fixed a, Fixed b) { return a.v_ > b.v_; }
	friend constexpr bool operator>=(Fixed a, Fixed b) { return a.v_ >= b.v_; }

	// --- compound assignment ---
	Fixed &operator+=(Fixed b) { v_ += b.v_; return *this; }
	Fixed &operator-=(Fixed b) { v_ -= b.v_; return *this; }
	Fixed &operator*=(int b) { v_ *= b; return *this; }
	Fixed &operator/=(int b) { v_ /= b; return *this; }
	Fixed &operator<<=(int n) { v_ <<= n; return *this; }
	Fixed &operator>>=(int n) { v_ >>= n; return *this; }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	Fixed &operator&=(M m) { v_ &= widenMask(m); return *this; }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	Fixed &operator|=(M m) { v_ |= widenMask(m); return *this; }

private:
	int64_t v_;
};

// [rc4l] abs()/MIN/MAX/clamp for Fixed, found by ADL so the engine's unqualified calls keep
// working. Same-type MIN(Fixed,Fixed) already resolves via the global template; these non-template
// overloads additionally let MIN(Fixed, intliteral) work (the int promotes to Fixed), which the
// global mixed-type overload can't do because it is gated on std::is_arithmetic.
inline constexpr Fixed abs(Fixed f) { return f.Raw() < 0 ? Fixed::FromRaw(-f.Raw()) : f; }
inline constexpr Fixed MIN(Fixed a, Fixed b) { return a < b ? a : b; }
inline constexpr Fixed MAX(Fixed a, Fixed b) { return a > b ? a : b; }
inline constexpr Fixed clamp(Fixed v, Fixed lo, Fixed hi) { return v < lo ? lo : (hi < v ? hi : v); }

} // namespace zx

#endif // ZX_FIXED_STRONG_H

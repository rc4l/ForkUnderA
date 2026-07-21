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

namespace zx
{

class Fixed
{
public:
	constexpr Fixed() : v_(0) {}

	// [rc4l] Signed integer sources sign-extend safely -> implicit is fine and keeps literals,
	// comparisons, and `+`/`-` with plain ints working without a flood of overloads.
	constexpr Fixed(int v) : v_(v) {}
	constexpr Fixed(long v) : v_(static_cast<int64_t>(v)) {}
	constexpr Fixed(long long v) : v_(static_cast<int64_t>(v)) {}

	// [rc4l] Unsigned sources are the hazard (zero-extension of a value that meant a signed
	// delta). Deleting them turns `fixed_t f = someAngle;` / `= someDWORD;` into a compile error.
	Fixed(unsigned) = delete;
	Fixed(unsigned long) = delete;
	Fixed(unsigned long long) = delete;

	// [rc4l] The blessed, visible ways to cross the boundary on purpose.
	constexpr int64_t Raw() const { return v_; }
	static constexpr Fixed FromRaw(int64_t v) { Fixed f; f.v_ = v; return f; }

	// [rc4l] Narrowing out of fixed is explicit only.
	explicit constexpr operator int() const { return static_cast<int>(v_); }
	explicit constexpr operator int64_t() const { return v_; }
	explicit constexpr operator unsigned() const { return static_cast<unsigned>(v_); }
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
	friend constexpr Fixed operator&(Fixed a, int64_t m) { return FromRaw(a.v_ & m); }
	friend constexpr Fixed operator|(Fixed a, int64_t m) { return FromRaw(a.v_ | m); }
	friend constexpr Fixed operator^(Fixed a, int64_t m) { return FromRaw(a.v_ ^ m); }
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
	Fixed &operator&=(int64_t m) { v_ &= m; return *this; }
	Fixed &operator|=(int64_t m) { v_ |= m; return *this; }

private:
	int64_t v_;
};

} // namespace zx

#endif // ZX_FIXED_STRONG_H

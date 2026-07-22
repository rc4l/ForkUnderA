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
// Copyright (C) 2026 rc4l
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

	// [rc4l] Explicit construction from floating point, truncating toward zero -- matches what the
	// non-strict build did for `fixed_t(doubleExpr)` / `(fixed_t)doubleExpr` (an (int64)double
	// conversion of a value that already carries fixed-point magnitude). Explicit, so an implicit
	// `fixed_t x = 1.5;` still won't compile. See FixedFromDouble tests in fixed_strong_test.cpp.
	explicit constexpr Fixed(double v) : v_(static_cast<long long>(v)) {}
	explicit constexpr Fixed(float v) : v_(static_cast<long long>(v)) {}

	// [rc4l] The blessed, visible ways to cross the boundary on purpose.
	constexpr int64_t Raw() const { return v_; }
	static constexpr Fixed FromRaw(int64_t v) { return Fixed(v); }

	// [rc4l] Narrowing / lossy conversions out of fixed are explicit only. Provided for every
	// standard arithmetic type (using the standard type names, not fixed-width aliases, to avoid
	// int64_t/long/long long redefinition clashes across platforms) so an explicit (T)fixed cast
	// to any target resolves to an exact operator -- no ambiguity, which the migration relies on.
	explicit constexpr operator bool() const { return v_ != 0; }
	explicit constexpr operator char() const { return static_cast<char>(v_); }
	explicit constexpr operator signed char() const { return static_cast<signed char>(v_); }
	explicit constexpr operator unsigned char() const { return static_cast<unsigned char>(v_); }
	explicit constexpr operator short() const { return static_cast<short>(v_); }
	explicit constexpr operator unsigned short() const { return static_cast<unsigned short>(v_); }
	explicit constexpr operator int() const { return static_cast<int>(v_); }
	explicit constexpr operator unsigned int() const { return static_cast<unsigned int>(v_); }
	explicit constexpr operator long() const { return static_cast<long>(v_); }
	explicit constexpr operator unsigned long() const { return static_cast<unsigned long>(v_); }
	explicit constexpr operator long long() const { return static_cast<long long>(v_); }
	explicit constexpr operator unsigned long long() const { return static_cast<unsigned long long>(v_); }
	explicit constexpr operator float() const { return static_cast<float>(v_); }
	explicit constexpr operator double() const { return static_cast<double>(v_); }

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

	// [rc4l] A floating-point operand to *, /, %, <<, >> is a silent-truncation hazard: `FRACUNIT *
	// sin(x)` bound to operator*(Fixed,int), truncating sin (in [0,1)) to 0 -- which zeroed the whole
	// finesine/finecosine table and killed movement, hitscan, sight and rendering at every
	// non-cardinal angle. These are deleted as SFINAE templates *after the class* (see below) so they
	// match only floating types -- a plain operator*(Fixed,double) would also make `long * Fixed`
	// ambiguous between the int/double/float overloads.

	// --- bit ops (shifts and masks are used pervasively on fixed values) ---
	friend constexpr Fixed operator<<(Fixed a, int n) { return FromRaw(a.v_ << n); }
	friend constexpr Fixed operator>>(Fixed a, int n) { return FromRaw(a.v_ >> n); }

	// [rc4l] Bitwise masks. A mask of 32 bits or fewer is reinterpreted as a 32-bit pattern and
	// SIGN-EXTENDED to 64 bits -- the author's 32-bit mental model. So a high-bit "clear the low N
	// bits" mask like 0xFFFFFE00 stays sign-preserving on the widened value instead of wiping the
	// sign (the polyobject-rotation bug), while low-bit masks (& 0xFFFF, & FINEMASK) are unchanged
	// because sign-extending a positive mask is a no-op. Genuine 64-bit masks pass through as-is.
	// This makes that whole bug class impossible -- including in backported code.
	// [rc4l] Split by size into two overloads rather than a `sizeof(M) <= 4 ? ... : ...` ternary: a
	// ternary whose condition is a compile-time constant leaves the untaken branch as a dead but
	// still coverage-mapped region in each instantiation, which reads as unexecuted and flakes the
	// 100% coverage gate across clang versions. Each overload carries only its own branch.
	template <class M, typename std::enable_if<std::is_integral<M>::value && (sizeof(M) <= 4), int>::type = 0>
	static constexpr int64_t widenMask(M m) { return int64_t(int32_t(m)); }  // <=32-bit mask: sign-extend
	template <class M, typename std::enable_if<std::is_integral<M>::value && (sizeof(M) > 4), int>::type = 0>
	static constexpr int64_t widenMask(M m) { return int64_t(m); }           // 64-bit mask: pass through
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator&(Fixed a, M m) { return FromRaw(a.v_ & widenMask(m)); }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator|(Fixed a, M m) { return FromRaw(a.v_ | widenMask(m)); }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	friend constexpr Fixed operator^(Fixed a, M m) { return FromRaw(a.v_ ^ widenMask(m)); }
	// [rc4l] Bare Fixed/Fixed division and modulo are the RAW integer operations (matching the
	// non-strict int64 build). This is a legitimate, non-overflowing idiom in the engine -- a
	// distance/speed scalar, coord/FRACUNIT to map units, a zdiff/dist slope. Code that wants a
	// fixed-point ratio uses FixedDiv/DivScale explicitly, so bare `/` is always the raw quotient.
	// (Fixed*Fixed is deliberately NOT provided -- a bare fixed multiply overflows, so it must be
	// spelled FixedMul or an explicit (int) cast, keeping that bug class a compile error.)
	friend constexpr Fixed operator/(Fixed a, Fixed b) { return FromRaw(a.v_ / b.v_); }
	friend constexpr Fixed operator%(Fixed a, Fixed b) { return FromRaw(a.v_ % b.v_); }

	// [rc4l] Bitwise ops between two fixed values (used for sign tricks like (dy ^ dx) >= 0).
	friend constexpr Fixed operator&(Fixed a, Fixed b) { return FromRaw(a.v_ & b.v_); }
	friend constexpr Fixed operator|(Fixed a, Fixed b) { return FromRaw(a.v_ | b.v_); }
	friend constexpr Fixed operator^(Fixed a, Fixed b) { return FromRaw(a.v_ ^ b.v_); }
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
	Fixed &operator/=(Fixed b) { v_ /= b.v_; return *this; } // raw quotient, matches operator/
	Fixed &operator%=(int b) { v_ %= b; return *this; }
	Fixed &operator%=(Fixed b) { v_ %= b.v_; return *this; }
	// [rc4l] Same silent-truncation hazard as the binary *, /, % above -- delete the float forms so
	// `fixed *= 0.6` is a compile error (write fixed = fixed_t(double(fixed) * 0.6) explicitly). SFINAE
	// on is_floating_point so integer compound ops (fixed *= someLong) still resolve to the int form.
	template <class F, class = typename std::enable_if<std::is_floating_point<F>::value>::type>
	Fixed &operator*=(F) = delete;
	template <class F, class = typename std::enable_if<std::is_floating_point<F>::value>::type>
	Fixed &operator/=(F) = delete;
	template <class F, class = typename std::enable_if<std::is_floating_point<F>::value>::type>
	Fixed &operator%=(F) = delete;
	Fixed &operator++() { ++v_; return *this; }
	Fixed &operator--() { --v_; return *this; }
	Fixed operator++(int) { Fixed t = *this; ++v_; return t; }
	Fixed operator--(int) { Fixed t = *this; --v_; return t; }
	Fixed &operator<<=(int n) { v_ <<= n; return *this; }
	Fixed &operator>>=(int n) { v_ >>= n; return *this; }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	Fixed &operator&=(M m) { v_ &= widenMask(m); return *this; }
	template <class M, class = typename std::enable_if<std::is_integral<M>::value>::type>
	Fixed &operator|=(M m) { v_ |= widenMask(m); return *this; }

private:
	int64_t v_;
};

// [rc4l] Delete `Fixed op <floating>` (and the reverse) for *, /, %, <<, >> as SFINAE templates that
// match ONLY floating-point types. This makes `FRACUNIT * sin(x)` and `alpha / 65536.0` hard compile
// errors -- forcing double(fixedval) for float math, FixedMul for fixed*fixed, or an explicit (int)
// -- while integer operands (long, unsigned, ...) still resolve to operator*(Fixed,int) without the
// ambiguity a plain deleted operator*(Fixed,double) would introduce.
#define ZX_DELETE_FIXED_FLOAT_OP(op) \
	template <class F, class = typename std::enable_if<std::is_floating_point<F>::value>::type> \
	Fixed operator op (Fixed, F) = delete; \
	template <class F, class = typename std::enable_if<std::is_floating_point<F>::value>::type> \
	Fixed operator op (F, Fixed) = delete;
ZX_DELETE_FIXED_FLOAT_OP(*)
ZX_DELETE_FIXED_FLOAT_OP(/)
ZX_DELETE_FIXED_FLOAT_OP(%)
ZX_DELETE_FIXED_FLOAT_OP(<<)
ZX_DELETE_FIXED_FLOAT_OP(>>)
#undef ZX_DELETE_FIXED_FLOAT_OP

// [rc4l] abs()/MIN/MAX/clamp for Fixed, found by ADL so the engine's unqualified calls keep
// working. Same-type MIN(Fixed,Fixed) already resolves via the global template; these non-template
// overloads additionally let MIN(Fixed, intliteral) work (the int promotes to Fixed), which the
// global mixed-type overload can't do because it is gated on std::is_arithmetic.
// [rc4l] The declarator names are parenthesized so a function-like macro (sys/param.h defines
// MIN/MAX; some libcs macro abs) can't expand into these definitions. Call sites still resolve:
// via ADL to these overloads when the names are functions, or via the macro (which works on Fixed
// through its operators) when they are macros.
inline constexpr Fixed (abs)(Fixed f) { return f.Raw() < 0 ? Fixed::FromRaw(-f.Raw()) : f; }
inline constexpr Fixed (MIN)(Fixed a, Fixed b) { return a < b ? a : b; }
inline constexpr Fixed (MAX)(Fixed a, Fixed b) { return a > b ? a : b; }
inline constexpr Fixed (clamp)(Fixed v, Fixed lo, Fixed hi) { return v < lo ? lo : (hi < v ? hi : v); }

} // namespace zx

#endif // ZX_FIXED_STRONG_H

#ifndef __BASICTYPES_H
#define __BASICTYPES_H

#ifdef _MSC_VER
typedef __int8					SBYTE;
typedef unsigned __int8			BYTE;
typedef __int16					SWORD;
typedef unsigned __int16		WORD;
typedef __int32					SDWORD;
typedef unsigned __int32		uint32;
typedef __int64					SQWORD;
typedef unsigned __int64		QWORD;
#else
#include <stdint.h>

typedef int8_t					SBYTE;
typedef uint8_t					BYTE;
typedef int16_t					SWORD;
typedef uint16_t				WORD;
typedef int32_t					SDWORD;
typedef uint32_t				uint32;
typedef int64_t					SQWORD;
typedef uint64_t				QWORD;
#endif

typedef SDWORD					int32;
typedef float					real32;
typedef double					real64;

// [BC] New additions.
typedef	unsigned short			USHORT;
typedef	short					SHORT;
#ifdef __WINE__
typedef unsigned int ULONG;
typedef int LONG;
#else
typedef	unsigned long			ULONG;
typedef	long					LONG;
#endif
typedef unsigned int			UINT;
typedef	int						INT;

// windef.h, included by windows.h, has its own incompatible definition
// of DWORD as a long. In files that mix Doom and Windows code, you
// must define USE_WINDOWS_DWORD before including doomtype.h so that
// you are aware that those files have a different DWORD than the rest
// of the source.

#ifndef USE_WINDOWS_DWORD
typedef uint32					DWORD;
#endif
typedef uint32					BITFIELD;
typedef int						INTBOOL;

// a 64-bit constant
#ifdef __GNUC__
#define CONST64(v) (v##LL)
#define UCONST64(v) (v##ULL)
#else
#define CONST64(v) ((SQWORD)(v))
#define UCONST64(v) ((QWORD)(v))
#endif

#if !defined(GUID_DEFINED)
#define GUID_DEFINED
typedef struct _GUID
{
    DWORD	Data1;
    WORD	Data2;
    WORD	Data3;
    BYTE	Data4[8];
} GUID;
#endif

union QWORD_UNION
{
	QWORD AsOne;
	struct
	{
#ifdef __BIG_ENDIAN__
		unsigned int Hi, Lo;
#else
		unsigned int Lo, Hi;
#endif
	};
};

//
// [rc4l] Fixed point, 64-bit as 48.16 (widened from 32-bit 16.16 for giant maps and slope
// precision; FRACBITS stays 16 so in-range math is bit-identical to the old 32-bit path).
//
#define FRACBITS						16
#define FRACUNIT						((fixed_t)1<<FRACBITS)

// [rc4l] Strong-fixed type, ON by default. The engine build defines ZX_STRONG_FIXED for every TU
// (see the ZX_STRONG_FIXED option in src/zandronum/src/CMakeLists.txt), making fixed_t the strong
// zx::Fixed type -- it rejects the implicit angle->fixed and fixed->int conversions behind the
// widening bugs (see features/fixed64/computation/fixed_strong.h). It is a transparent wrapper
// over the same 64-bit integer: every operator forwards to the identical integer op, so the
// numbers are unchanged; only the dangerous conversions become a compile error instead of a
// silent bug. Plain C (which can't use a class) always gets the raw typedef. The define is set by
// the build rather than here so it reaches order-independently every TU, including headers like
// computation/fixedmath.h whose ZX_STRONG_FIXED shims must agree with this typedef. Configure the
// engine with -DZX_STRONG_FIXED=OFF to opt back into the raw integer for backporting upstream
// Zandronum/GZDoom patches, which assume a raw fixed_t.
#if defined(ZX_STRONG_FIXED) && defined(__cplusplus)
#include "features/fixed64/computation/fixed_strong.h"
typedef zx::Fixed						fixed_t;
#else
typedef SQWORD							fixed_t;
#endif
typedef DWORD							dsfixed_t;				// fixedpt used by span drawer

#define FIXED_MAX						((fixed_t)0x7fffffffffffffffLL)
#define FIXED_MIN						((fixed_t)0x8000000000000000LL)

#define DWORD_MIN						((uint32)0)
#define DWORD_MAX						((uint32)0xffffffff)


#ifdef __GNUC__
#define GCCPRINTF(stri,firstargi)		__attribute__((format(printf,stri,firstargi)))
#define GCCFORMAT(stri)					__attribute__((format(printf,stri,0)))
#define GCCNOWARN						__attribute__((unused))
#else
#define GCCPRINTF(a,b)
#define GCCFORMAT(a)
#define GCCNOWARN
#endif


#endif

// [Doruk-Aksoy] UZDoom's TRNSLATE lump (named translations) and the palette lumps it can name, parsed without the engine.
#ifndef ZX_TRNSLATE_COMPUTE_H
#define ZX_TRNSLATE_COMPUTE_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace zx { namespace trnslate {

// [Doruk-Aksoy] The highest standard translation a definition can start from, as UZDoom allows.
constexpr int kMaxStandardTranslation = 6;

// [Doruk-Aksoy] The most colours a palette lump gives.
constexpr int kPaletteColors = 256;

// [Doruk-Aksoy] What a definition's table starts as before its entries are added.
enum TrnslateBaseKind
{
	kBaseIdentity,
	kBaseStandard,
	kBaseNamed,
};

// [Doruk-Aksoy] One quoted entry: a palette lump's colours from start, or a range string for AddToTranslation.
struct TrnslateEntry
{
	std::string text;
	bool palette;
	int start;
};

// [Doruk-Aksoy] One `Name [: base] = "entry", ...` definition and the line it began on.
struct TrnslateDef
{
	std::string name;
	TrnslateBaseKind baseKind;
	int baseStandard;
	std::string baseName;
	std::vector<TrnslateEntry> entries;
	int line;
};

// [Doruk-Aksoy] Every definition before the first error, and that error (empty when the lump parsed).
struct TrnslateParse
{
	std::vector<TrnslateDef> defs;
	std::string error;
	int errorLine;
};

// [Doruk-Aksoy] Parses a TRNSLATE lump, isPaletteLump saying which quoted entries name a palette lump.
TrnslateParse ComputeParseTrnslate(const char *text, size_t length,
	const std::function<bool(const std::string &)> &isPaletteLump);

// [Doruk-Aksoy] The names UZDoom resolves before any TRNSLATE definition.
enum TrnslateBuiltin
{
	kNotBuiltin,
	kBuiltinNone,
	kBuiltinIce,
	kBuiltinPlayer,
};

// [Doruk-Aksoy] Whether a name is None, Ice or Player1 to Player8 (player set to 0 to 7), case-insensitively.
TrnslateBuiltin ComputeTrnslateBuiltin(const char *name, int *player);

// [Doruk-Aksoy] Reads a PNG's PLTE, a JASC-PAL text or a raw palette into out, returning its colour count or -1 with error set.
int ComputeReadPaletteLump(const unsigned char *data, size_t size, unsigned char out[kPaletteColors * 3],
	std::string *error);

// [Doruk-Aksoy] Whether count colours from start stay inside a 256-entry table, as UZDoom's AddColors requires.
bool ComputePaletteSpanFits(int start, int count);

// [Doruk-Aksoy] What A_SetTranslation does with its name.
enum SetTranslationOutcome
{
	kSetTranslationDefault,
	kSetTranslationFound,
	kSetTranslationKeep,
};

// [Doruk-Aksoy] A_fua_ResetTranslation restores the class's translation, A_SetTranslation applies a known name (None clearing it) and ignores an unknown one.
SetTranslationOutcome ComputeSetTranslationOutcome(bool resetToDefault, bool found);

}} // namespace zx::trnslate

#endif // ZX_TRNSLATE_COMPUTE_H

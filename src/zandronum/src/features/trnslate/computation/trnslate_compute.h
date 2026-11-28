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

// [Doruk-Aksoy] How a range remaps its palette entries, in the numbers SVC_CREATETRANSLATION sends.
enum TranslationRangeKind
{
	kRangePalette,		// [Doruk-Aksoy] "a:b=c:d", ACS TRANSLATIONRANGE1
	kRangeRgb,			// [Doruk-Aksoy] "a:b=[r,g,b]:[r,g,b]", TRANSLATIONRANGE2
	kRangeDesaturated,	// [Doruk-Aksoy] "a:b=%[r,g,b]:[r,g,b]", TRANSLATIONRANGE3
	kRangeColourise,	// [Doruk-Aksoy] "a:b=#[r,g,b]", TRANSLATIONRANGE4
	kRangeTint,			// [Doruk-Aksoy] "a:b=@amount[r,g,b]", TRANSLATIONRANGE5
	kRangeKinds,
};

// [Doruk-Aksoy] One range of a translation, which a script adds, the server sends and a joiner gets again: colourise and tint use r1, g1 and b1.
struct TranslationRange
{
	int kind = kRangePalette;
	int start = 0, end = 0;
	int pal1 = 0, pal2 = 0;
	int r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
	float fr1 = 0, fg1 = 0, fb1 = 0, fr2 = 0, fg2 = 0, fb2 = 0;
	int amount = 0;
};

// [Doruk-Aksoy] A colour as a remap table stores it, each channel a byte as UZDoom's PalEntry keeps the low 8 bits of what it is given.
struct RangeColor
{
	unsigned char r, g, b;
};

// [Doruk-Aksoy] UZDoom's AddColourisation for one palette colour: the target colour scaled by the colour's luminance (.299, .587, .114).
RangeColor ComputeColourise(int baseR, int baseG, int baseB, int r, int g, int b);

// [Doruk-Aksoy] UZDoom's AddTint for one palette colour: `amount` percent of the way from the colour to the target.
RangeColor ComputeTint(int baseR, int baseG, int baseB, int r, int g, int b, int amount);

// [Doruk-Aksoy] Whether a palette index names one of the 256 entries, as UZDoom's IndexOutOfRange checks each range's ends.
bool ComputePaletteIndexFits(int index);

// [Doruk-Aksoy] Whether a range names only palette entries, its ends and a palette range's targets, so a table takes it rather than refusing it.
bool ComputeTranslationRangeFits(const TranslationRange &range);

// [Doruk-Aksoy] A range's fields in the order SVC_CREATETRANSLATION carries them after its index and edited bit, written to anything with
// shortByte, byte, longValue and floatValue; a kind past kRangeKinds writes only its kind.
template <class Sink> void ComputeWriteTranslationRange(Sink &out, const TranslationRange &range)
{
	out.shortByte(range.kind, 7);
	if (range.kind < 0 || range.kind >= kRangeKinds)
		return;
	out.byte(range.start);
	out.byte(range.end);
	switch (range.kind)
	{
	case kRangePalette: out.byte(range.pal1); out.byte(range.pal2); break;
	case kRangeRgb:
		out.byte(range.r1); out.byte(range.g1); out.byte(range.b1);
		out.byte(range.r2); out.byte(range.g2); out.byte(range.b2);
		break;
	case kRangeDesaturated:
		out.floatValue(range.fr1); out.floatValue(range.fg1); out.floatValue(range.fb1);
		out.floatValue(range.fr2); out.floatValue(range.fg2); out.floatValue(range.fb2);
		break;
	case kRangeColourise: out.longValue(range.r1); out.longValue(range.g1); out.longValue(range.b1); break;
	default: out.longValue(range.amount); out.longValue(range.r1); out.longValue(range.g1); out.longValue(range.b1); break;
	}
}

// [Doruk-Aksoy] The range ComputeWriteTranslationRange wrote, read from anything with shortByte, byte, longValue and floatValue; false for a
// kind this build doesn't know, whose fields it can't step over.
template <class Source> bool ComputeReadTranslationRange(Source &in, TranslationRange &range)
{
	range.kind = in.shortByte(7);
	if (range.kind < 0 || range.kind >= kRangeKinds)
		return false;
	range.start = in.byte();
	range.end = in.byte();
	switch (range.kind)
	{
	case kRangePalette: range.pal1 = in.byte(); range.pal2 = in.byte(); break;
	case kRangeRgb:
		range.r1 = in.byte(); range.g1 = in.byte(); range.b1 = in.byte();
		range.r2 = in.byte(); range.g2 = in.byte(); range.b2 = in.byte();
		break;
	case kRangeDesaturated:
		range.fr1 = in.floatValue(); range.fg1 = in.floatValue(); range.fb1 = in.floatValue();
		range.fr2 = in.floatValue(); range.fg2 = in.floatValue(); range.fb2 = in.floatValue();
		break;
	case kRangeColourise: range.r1 = in.longValue(); range.g1 = in.longValue(); range.b1 = in.longValue(); break;
	default: range.amount = in.longValue(); range.r1 = in.longValue(); range.g1 = in.longValue(); range.b1 = in.longValue(); break;
	}
	return true;
}

}} // namespace zx::trnslate

#endif // ZX_TRNSLATE_COMPUTE_H

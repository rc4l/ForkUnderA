// [Doruk-Aksoy] What a LANGUAGE or LMACROS lump says, read without the engine: escapes, CSV cells, language headers, game filters and gender macros.
#ifndef ZX_LANGUAGEPARSE_COMPUTE_H
#define ZX_LANGUAGEPARSE_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zx { namespace language {

// [Doruk-Aksoy] The text colour escape '\c' turns into, which v_text.h names TEXTCOLOR_ESCAPE.
constexpr char kLanguageColorEscape = '\x1c';

// [Doruk-Aksoy] Language codes are packed as MAKE_ID packs them on a little-endian machine.
constexpr uint32_t ComputeLanguageCode(char a, char b, char c)
{
	return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16);
}

// [Doruk-Aksoy] The '*' table, which every language reads first.
constexpr uint32_t kLanguageGlobal = ComputeLanguageCode('*', 0, 0);

// [Doruk-Aksoy] The 'default' table, which every language falls back to.
constexpr uint32_t kLanguageDefault = ComputeLanguageCode('*', '*', 0);

// [Doruk-Aksoy] Turns \n \r \t \c and \xHH escapes into their characters in place and returns the new length; a trailing backslash ends the string.
size_t ComputeLanguageEscapes(char *str);

// [Doruk-Aksoy] Whether a LANGUAGE lump is the CSV form, which starts with a 'default,' or 'identifier,' header cell.
bool ComputeIsLanguageCSV(const char *buffer, size_t size);

// [Doruk-Aksoy] The rows of a CSV lump as Google Docs exports it, with doubled quotes unescaped and each cell's backslash escapes applied.
std::vector<std::vector<std::string>> ComputeParseLanguageCSV(const char *buffer, size_t size);

// [Doruk-Aksoy] One language header, '[enu default]' in the text form or one language of a CSV column.
struct LanguageHeader
{
	std::vector<uint32_t> codes;
	bool isDefault = false;
	bool emptyCode = false;
	bool tilde = false;
};

// [Doruk-Aksoy] Reads a header's tokens: '*' and 'default' take the header to themselves, two- and three-letter codes join it, '~' and other codes are ignored.
LanguageHeader ComputeLanguageHeader(const std::vector<std::string> &tokens);

// [Doruk-Aksoy] Whether a header has a code for a pass's language, by all three letters or, when not exact, the first two.
bool ComputeLanguageHeaderMatches(const LanguageHeader &header, uint32_t code, bool exact);

// [Doruk-Aksoy] One pass over the LANGUAGE lumps, its number being its index plus one, a lower number taking priority.
struct LanguagePass
{
	uint32_t code;
	bool exact;
};

// [Doruk-Aksoy] The passes for the four system language ids: '*', then each id whole, by its first two letters and by its region-less match, then 'default' alone.
std::vector<LanguagePass> ComputeLanguagePasses(const uint32_t ids[4], bool defaultOnly);

// [Doruk-Aksoy] Whether a space-separated CSV filter names the running game, an empty filter naming every game.
bool ComputeLanguageFilterAllows(const std::string &filter, const char *gameName);

// [Doruk-Aksoy] The columns of a CSV lump's header row, which is the only row naming them.
struct LanguageCSVLayout
{
	struct Column
	{
		size_t column;
		LanguageHeader header;
	};
	int labelColumn = -1;
	int filterColumn = -1;
	bool isDefault = false;
	std::vector<Column> languages;
};

// [Doruk-Aksoy] Reads a CSV header row: 'identifier', 'filter', 'remarks', and every other cell's space-separated languages.
LanguageCSVLayout ComputeLanguageCSVLayout(const std::vector<std::string> &header);

// [Doruk-Aksoy] One change a CSV lump makes for a pass: set a string, remove it for an empty cell, or drop older files' text for a label a 'default' CSV names.
struct LanguageCSVOp
{
	enum Kind { Set, Remove, PurgeOlder };
	Kind kind;
	std::string label;
	std::string text;
};

// [Doruk-Aksoy] The changes a CSV lump makes for one pass, row by row and column by column, skipping rows the filter rules out or with no label.
std::vector<LanguageCSVOp> ComputeLanguageCSVOps(const std::vector<std::vector<std::string>> &rows, const LanguageCSVLayout &layout, uint32_t code, bool exact, const char *gameName);

// [Doruk-Aksoy] Compares two strings as stricmp does for ASCII, so labels and macro names ignore case.
int ComputeLanguageCompareNoCase(const char *a, const char *b);

// [Doruk-Aksoy] Orders labels and macro names without regard to ASCII case, as ZDoom's names compare.
struct LanguageNameLess
{
	using is_transparent = void;
	bool operator()(const std::string &a, const std::string &b) const;
	bool operator()(const std::string &a, const char *b) const;
	bool operator()(const char *a, const std::string &b) const;
};

// [Doruk-Aksoy] A string's text for each gender column: male, female, neutral and object, or one text for all.
using LanguageGenderStrings = std::vector<std::string>;

// [Doruk-Aksoy] The macros of every LMACROS lump, a later lump's macro replacing an earlier one of the same name.
using LanguageMacros = std::map<std::string, LanguageGenderStrings, LanguageNameLess>;

// [Doruk-Aksoy] Adds an LMACROS lump's rows, 'name,language,male,female,neutral,object' after one header row, a missing cell being empty.
void ComputeReadLanguageMacros(LanguageMacros &macros, const char *buffer, size_t size);

// [Doruk-Aksoy] A string's texts after its macros are replaced, and whether an '@[' had no ']' after it.
struct LanguageMacroResult
{
	LanguageGenderStrings strings;
	bool badMacro = false;
};

// [Doruk-Aksoy] Replaces each '@[name]' left to right with the macro's text per gender, an unknown macro with nothing, giving one text when there is no macro.
LanguageMacroResult ComputeSubstituteLanguageMacros(const std::string &text, const LanguageMacros &macros);

// [Doruk-Aksoy] The 'Translation not found' line for a missing label and the key it is reported once under, cut after maxlen characters.
struct LanguageMissingReport
{
	std::string key;
	std::string message;
};

// [Doruk-Aksoy] Builds the report for a missing label, with control characters written as their escapes.
LanguageMissingReport ComputeLanguageMissingReport(const char *name, size_t maxlen);

}} // namespace zx::language

#endif

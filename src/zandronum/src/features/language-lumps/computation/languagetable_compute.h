// [Doruk-Aksoy] The string table's entries without the engine: which LANGUAGE pass wins a label, '$$' aliases, gender texts and Dehacked's lookups.
#ifndef ZX_LANGUAGETABLE_COMPUTE_H
#define ZX_LANGUAGETABLE_COMPUTE_H

#include <cstddef>
#include <map>
#include <string>

#include "languageparse_compute.h"

namespace zx { namespace language {

// [Doruk-Aksoy] A label's texts, the pass that set them (0 for Dehacked) and the resource file they came from.
struct LanguageEntry
{
	LanguageGenderStrings strings;
	int pass;
	int file;
};

// [Doruk-Aksoy] Every label the running language reads, its case ignored.
using LanguageTable = std::map<std::string, LanguageEntry, LanguageNameLess>;

// [Doruk-Aksoy] The pass Dehacked's strings carry, which no LANGUAGE lump replaces.
constexpr int kLanguageDehackedPass = 0;

// [Doruk-Aksoy] How many '$$' steps a lookup follows before taking the label as missing, so a loop of aliases ends.
constexpr int kLanguageMaxAliasDepth = 32;

// [Doruk-Aksoy] Whether a string from a pass and file replaces a label's entry: never Dehacked's, else when its pass ranks as high, or when it is default text from a later file.
bool ComputeLanguageReplaces(const LanguageEntry &existing, int pass, int file, bool isDefault);

// [Doruk-Aksoy] Sets a label's texts when no entry has it or ComputeLanguageReplaces lets them replace it.
void ComputeLanguageInsert(LanguageTable &table, const std::string &label, const LanguageGenderStrings &strings, int pass, int file, bool isDefault);

// [Doruk-Aksoy] Drops a label's entry set by the same pass, as an empty CSV cell removes its language's text.
void ComputeLanguageRemove(LanguageTable &table, const std::string &label, int pass);

// [Doruk-Aksoy] Drops a label's entry from a file older than file, unless Dehacked set it.
void ComputeLanguagePurgeOlder(LanguageTable &table, const std::string &label, int file);

// [Doruk-Aksoy] Sets a label to Dehacked's text, which survives a reload of the LANGUAGE lumps.
void ComputeLanguageSet(LanguageTable &table, const char *label, const char *text);

// [Doruk-Aksoy] Drops every entry the LANGUAGE lumps set, keeping Dehacked's.
void ComputeLanguageClearLoaded(LanguageTable &table);

// [Doruk-Aksoy] The gender column a player's gender reads: male, female, and 'other' as the object column.
size_t ComputeLanguageGenderIndex(int gender);

// [Doruk-Aksoy] The label a text names when it starts with '$$', or null for plain text.
const char *ComputeLanguageAliasTarget(const char *text);

// [Doruk-Aksoy] A label's text for a gender column, following '$$' aliases, or null when it or an alias's target is missing or the aliases loop.
const char *ComputeLanguageFind(const LanguageTable &table, const char *label, size_t gender);

// [Doruk-Aksoy] The label whose first text equals text, ignoring case, or null; Dehacked finds stock strings this way.
const char *ComputeLanguageMatch(const LanguageTable &table, const char *text);

}} // namespace zx::language

#endif

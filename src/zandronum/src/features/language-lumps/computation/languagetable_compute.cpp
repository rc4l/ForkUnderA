// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad src/common/engine/stringtable.cpp onto Zandronum's pass-ranked table.
#include "languagetable_compute.h"

namespace zx { namespace language {

bool ComputeLanguageReplaces(const LanguageEntry &existing, int pass, int file, bool isDefault)
{
	if (existing.pass == kLanguageDehackedPass) return false;
	if (existing.pass >= pass) return true;
	// [Doruk-Aksoy] uzdoom@68c33a6e43: default text from a later file outranks an older file's translation.
	return isDefault && existing.file < file;
}

void ComputeLanguageInsert(LanguageTable &table, const std::string &label, const LanguageGenderStrings &strings, int pass, int file, bool isDefault)
{
	LanguageTable::iterator it = table.find(label);
	if (it == table.end())
	{
		table.insert(LanguageTable::value_type(label, LanguageEntry{ strings, pass, file }));
	}
	else if (ComputeLanguageReplaces(it->second, pass, file, isDefault))
	{
		it->second = LanguageEntry{ strings, pass, file };
	}
}

void ComputeLanguageRemove(LanguageTable &table, const std::string &label, int pass)
{
	LanguageTable::iterator it = table.find(label);
	if (it != table.end() && it->second.pass == pass) table.erase(it);
}

void ComputeLanguagePurgeOlder(LanguageTable &table, const std::string &label, int file)
{
	LanguageTable::iterator it = table.find(label);
	if (it != table.end() && it->second.pass != kLanguageDehackedPass && it->second.file < file) table.erase(it);
}

void ComputeLanguageSet(LanguageTable &table, const char *label, const char *text)
{
	table[label] = LanguageEntry{ LanguageGenderStrings(1, text), kLanguageDehackedPass, 0 };
}

void ComputeLanguageClearLoaded(LanguageTable &table)
{
	for (LanguageTable::iterator it = table.begin(); it != table.end();)
	{
		if (it->second.pass != kLanguageDehackedPass) it = table.erase(it);
		else ++it;
	}
}

size_t ComputeLanguageGenderIndex(int gender)
{
	// [Doruk-Aksoy] Zandronum's 'other' is UZDoom's 'it', the fourth column, since nothing here picks the neutral 'they' column.
	switch (gender)
	{
	case 1: return 1;
	case 2: return 3;
	default: return 0;
	}
}

const char *ComputeLanguageAliasTarget(const char *text)
{
	return (text[0] == '$' && text[1] == '$') ? text + 2 : nullptr;
}

const char *ComputeLanguageFind(const LanguageTable &table, const char *label, size_t gender)
{
	if (label == nullptr) return nullptr;
	// [Doruk-Aksoy] uzdoom@96ceb11af0 follows '$$' without a bound, so A = "$$B" and B = "$$A" overflowed its stack.
	for (int depth = 0; depth <= kLanguageMaxAliasDepth; ++depth)
	{
		LanguageTable::const_iterator it = table.find(label);
		if (it == table.end()) return nullptr;
		const LanguageGenderStrings &strings = it->second.strings;
		const char *text = strings[gender < strings.size() ? gender : 0].c_str();
		const char *target = ComputeLanguageAliasTarget(text);
		if (target == nullptr) return text;
		label = target;
	}
	return nullptr;
}

const char *ComputeLanguageMatch(const LanguageTable &table, const char *text)
{
	// [Doruk-Aksoy] uzdoom@d5f138f14d: case is ignored, so a recapitalised string table still matches old Dehacked text.
	for (const LanguageTable::value_type &entry : table)
	{
		if (ComputeLanguageCompareNoCase(entry.second.strings[0].c_str(), text) == 0) return entry.first.c_str();
	}
	return nullptr;
}

}} // namespace zx::language

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad src/common/engine/stringtable.cpp, minus the engine.
#include "languageparse_compute.h"

#include <utility>

namespace zx { namespace language {

namespace {

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool EqualsNoCase(const std::string &a, const char *b)
{
	return ComputeLanguageCompareNoCase(a.c_str(), b) == 0;
}

bool StartsWithNoCase(const char *buffer, const char *prefix)
{
	for (; *prefix != 0; ++buffer, ++prefix)
	{
		if (LowerAscii(*buffer) != LowerAscii(*prefix)) return false;
	}
	return true;
}

std::vector<std::string> SplitSpaces(const std::string &str)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (start < str.size())
	{
		size_t end = str.find(' ', start);
		if (end == std::string::npos) end = str.size();
		if (end > start) parts.push_back(str.substr(start, end - start));
		start = end + 1;
	}
	return parts;
}

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string StripSpaces(const std::string &str)
{
	size_t start = 0, end = str.size();
	while (start < end && IsSpace(str[start])) ++start;
	while (end > start && IsSpace(str[end - 1])) --end;
	return str.substr(start, end - start);
}

int HexDigit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

std::string EscapedCell(std::string cell)
{
	cell.push_back('\0');
	ComputeLanguageEscapes(&cell[0]);
	return std::string(cell.c_str());
}

} // namespace

int ComputeLanguageCompareNoCase(const char *a, const char *b)
{
	for (;; ++a, ++b)
	{
		const unsigned char ca = (unsigned char)LowerAscii(*a), cb = (unsigned char)LowerAscii(*b);
		if (ca != cb || ca == 0) return int(ca) - int(cb);
	}
}

size_t ComputeLanguageEscapes(char *iptr)
{
	char *sptr = iptr, *optr = iptr, c;

	while ((c = *iptr++) != '\0')
	{
		if (c == '\\')
		{
			c = *iptr++;
			// [Doruk-Aksoy] A trailing backslash ends the string, where upstream read on past its terminator.
			if (c == '\0') break;
			if (c == 'n')
				c = '\n';
			else if (c == 'c')
				c = kLanguageColorEscape;
			else if (c == 'r')
				c = '\r';
			else if (c == 't')
				c = '\t';
			else if (c == 'x')
			{
				unsigned value = 0;
				for (int i = 0; i < 2; i++)
				{
					const int digit = HexDigit(*iptr);
					if (digit < 0) break;
					value = (value << 4) + unsigned(digit);
					iptr++;
				}
				if (value == 0) continue;
				c = char(value);
			}
			else if (c == '\n')
				continue;
		}
		*optr++ = c;
	}
	*optr = '\0';
	return size_t(optr - sptr);
}

bool ComputeIsLanguageCSV(const char *buffer, size_t size)
{
	if (size < 11) return false;
	return StartsWithNoCase(buffer, "default,") || StartsWithNoCase(buffer, "identifier,");
}

std::vector<std::vector<std::string>> ComputeParseLanguageCSV(const char *buffer, size_t size)
{
	std::vector<std::vector<std::string>> data;
	std::vector<std::string> row;
	std::string cell;
	bool quoted = false;

	for (size_t i = 0; i < size; ++i)
	{
		const char c = buffer[i];
		if (c == '"')
		{
			// [Doruk-Aksoy] Doubled quotes inside a quoted cell are one quotation mark.
			if (quoted && i + 1 < size && buffer[i + 1] == '"')
			{
				cell.push_back('"');
				i++;
			}
			else if (cell.empty() || quoted)
			{
				quoted = !quoted;
			}
		}
		else if (c == ',' && !quoted)
		{
			row.push_back(EscapedCell(cell));
			cell.clear();
		}
		else if (c == '\r')
		{
			// [Doruk-Aksoy] Carriage returns are dropped everywhere, as upstream drops them.
		}
		else if (c == '\n' && !quoted)
		{
			row.push_back(EscapedCell(cell));
			data.push_back(std::move(row));
			row.clear();
			cell.clear();
		}
		else
		{
			cell.push_back(c);
		}
	}

	if (!cell.empty() || !row.empty())
	{
		row.push_back(EscapedCell(cell));
		data.push_back(std::move(row));
	}
	return data;
}

LanguageHeader ComputeLanguageHeader(const std::vector<std::string> &tokens)
{
	LanguageHeader header;
	for (const std::string &token : tokens)
	{
		if (token.empty())
		{
			header.emptyCode = true;
		}
		else if (token == "~")
		{
			header.tilde = true;
		}
		else if (token == "*")
		{
			header.codes.assign(1, kLanguageGlobal);
		}
		else if (EqualsNoCase(token, "default"))
		{
			header.codes.assign(1, kLanguageDefault);
			header.isDefault = true;
		}
		else if (token.size() == 2 || token.size() == 3)
		{
			// [Doruk-Aksoy] uzdoom@5f2d41884c: a header naming '*' or 'default' keeps that table to itself.
			const bool reserved = header.codes.size() == 1 && (header.codes[0] == kLanguageGlobal || header.codes[0] == kLanguageDefault);
			if (!reserved) header.codes.push_back(ComputeLanguageCode(LowerAscii(token[0]), LowerAscii(token[1]), token.size() == 3 ? LowerAscii(token[2]) : 0));
		}
	}
	return header;
}

bool ComputeLanguageHeaderMatches(const LanguageHeader &header, uint32_t code, bool exact)
{
	const uint32_t orMask = exact ? 0 : ComputeLanguageCode(0, 0, char(0xff));
	for (uint32_t c : header.codes)
	{
		if ((c | orMask) == (code | orMask)) return true;
	}
	return false;
}

std::vector<LanguagePass> ComputeLanguagePasses(const uint32_t ids[4], bool defaultOnly)
{
	std::vector<LanguagePass> passes;
	if (!defaultOnly)
	{
		passes.push_back({ kLanguageGlobal, true });
		for (int i = 0; i < 4; ++i)
		{
			passes.push_back({ ids[i], true });
			passes.push_back({ ids[i] & ComputeLanguageCode(char(0xff), char(0xff), 0), true });
			passes.push_back({ ids[i], false });
		}
	}
	passes.push_back({ kLanguageDefault, true });
	return passes;
}

bool ComputeLanguageFilterAllows(const std::string &filter, const char *gameName)
{
	const std::vector<std::string> games = SplitSpaces(filter);
	if (games.empty()) return true;
	if (gameName == nullptr) return false;
	for (const std::string &game : games)
	{
		if (EqualsNoCase(game, gameName)) return true;
	}
	return false;
}

LanguageCSVLayout ComputeLanguageCSVLayout(const std::vector<std::string> &header)
{
	LanguageCSVLayout layout;
	for (size_t column = 0; column < header.size(); ++column)
	{
		const std::string &entry = header[column];
		if (EqualsNoCase(entry, "filter"))
		{
			layout.filterColumn = int(column);
		}
		else if (EqualsNoCase(entry, "identifier"))
		{
			layout.labelColumn = int(column);
		}
		else if (!EqualsNoCase(entry, "remarks"))
		{
			for (const std::string &language : SplitSpaces(entry))
			{
				LanguageHeader one = ComputeLanguageHeader({ language });
				if (one.codes.empty()) continue;
				layout.isDefault |= one.isDefault;
				layout.languages.push_back({ column, one });
			}
		}
	}
	return layout;
}

std::vector<LanguageCSVOp> ComputeLanguageCSVOps(const std::vector<std::vector<std::string>> &rows, const LanguageCSVLayout &layout, uint32_t code, bool exact, const char *gameName)
{
	std::vector<LanguageCSVOp> ops;
	if (layout.labelColumn < 0) return ops;
	const size_t labelColumn = size_t(layout.labelColumn);

	for (size_t i = 1; i < rows.size(); ++i)
	{
		const std::vector<std::string> &row = rows[i];
		if (layout.filterColumn >= 0 && size_t(layout.filterColumn) < row.size() && !ComputeLanguageFilterAllows(row[size_t(layout.filterColumn)], gameName)) continue;
		if (labelColumn >= row.size()) continue;
		// [Doruk-Aksoy] uzdoom@6f2bc8379a: labels lose the spaces around them.
		const std::string label = StripSpaces(row[labelColumn]);
		if (label.empty()) continue;

		// [Doruk-Aksoy] uzdoom@4668fa95e3: a mod's new default text outranks every older file's translation of it.
		if (layout.isDefault) ops.push_back({ LanguageCSVOp::PurgeOlder, label, std::string() });
		for (const LanguageCSVLayout::Column &language : layout.languages)
		{
			if (language.column >= row.size() || !ComputeLanguageHeaderMatches(language.header, code, exact)) continue;
			const std::string &text = row[language.column];
			ops.push_back({ text.empty() ? LanguageCSVOp::Remove : LanguageCSVOp::Set, label, text });
		}
	}
	return ops;
}

bool LanguageNameLess::operator()(const std::string &a, const std::string &b) const
{
	return ComputeLanguageCompareNoCase(a.c_str(), b.c_str()) < 0;
}

bool LanguageNameLess::operator()(const std::string &a, const char *b) const
{
	return ComputeLanguageCompareNoCase(a.c_str(), b) < 0;
}

bool LanguageNameLess::operator()(const char *a, const std::string &b) const
{
	return ComputeLanguageCompareNoCase(a, b.c_str()) < 0;
}

void ComputeReadLanguageMacros(LanguageMacros &macros, const char *buffer, size_t size)
{
	const std::vector<std::vector<std::string>> rows = ComputeParseLanguageCSV(buffer, size);
	for (size_t i = 1; i < rows.size(); ++i)
	{
		const std::vector<std::string> &row = rows[i];
		if (row[0].empty()) continue;
		LanguageGenderStrings strings(4);
		// [Doruk-Aksoy] A short row leaves its missing genders empty, where upstream read past the row.
		for (size_t k = 0; k < 4 && k + 2 < row.size(); ++k) strings[k] = row[k + 2];
		macros[row[0]] = strings;
	}
}

LanguageMacroResult ComputeSubstituteLanguageMacros(const std::string &text, const LanguageMacros &macros)
{
	LanguageMacroResult result;
	size_t pos = text.find("@[");
	if (pos == std::string::npos)
	{
		result.strings.assign(1, text);
		return result;
	}

	result.strings.assign(4, std::string());
	size_t start = 0;
	while (pos != std::string::npos)
	{
		const size_t end = text.find(']', pos);
		if (end == std::string::npos)
		{
			result.badMacro = true;
			break;
		}
		const LanguageMacros::const_iterator macro = macros.find(text.substr(pos + 2, end - pos - 2));
		for (size_t g = 0; g < 4; ++g)
		{
			result.strings[g].append(text, start, pos - start);
			if (macro != macros.end()) result.strings[g] += macro->second[g];
		}
		start = end + 1;
		pos = text.find("@[", start);
	}
	for (size_t g = 0; g < 4; ++g) result.strings[g].append(text, start, std::string::npos);
	return result;
}

LanguageMissingReport ComputeLanguageMissingReport(const char *name, size_t maxlen)
{
	static const char escapes[][2] = {
		{ '\a', 'a' }, { '\b', 'b' }, { '\t', 't' },
		{ '\n', 'n' }, { '\f', 'f' }, { '\r', 'r' },
	};

	LanguageMissingReport report;
	report.key = name;
	const bool truncated = report.key.size() > maxlen;
	if (truncated)
	{
		report.key.resize(maxlen);
		report.key += "...";
	}

	report.message = "Translation not found ";
	if (truncated) report.message += "(truncated) ";
	report.message += '\'';
	for (char c : report.key)
	{
		for (const char *escape : escapes)
		{
			if (c != escape[0]) continue;
			c = escape[1];
			report.message += '\\';
			break;
		}
		report.message += c;
	}
	report.message += '\'';
	return report;
}

}} // namespace zx::language

// [Doruk-Aksoy] UZDoom's TRNSLATE parser (uzdoom@a505e910, r_translate.cpp R_ParseTrnslate) and ReadPalette (uzdoom@0c04cddd), engine-free.
#include "features/trnslate/computation/trnslate_compute.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace zx { namespace trnslate {

namespace {

enum TokenKind
{
	kTokEnd,
	kTokError,
	kTokIdentifier,
	kTokInt,
	kTokFloat,
	kTokString,
	kTokPunct,
};

struct Token
{
	TokenKind kind;
	std::string text;
	long number;
	int line;
};

// [Doruk-Aksoy] The C-mode tokens FScanner gives a TRNSLATE lump: words, numbers, strings and single characters.
struct Lexer
{
	const char *text;
	size_t length;
	size_t pos;
	int line;

	bool SkipSpaceAndComments(Token *tok)
	{
		while (pos < length)
		{
			const char c = text[pos];
			if (c == '\n') { line++; pos++; }
			else if (std::isspace((unsigned char)c)) pos++;
			else if (c == '/' && pos + 1 < length && text[pos + 1] == '/')
			{
				while (pos < length && text[pos] != '\n') pos++;
			}
			else if (c == '/' && pos + 1 < length && text[pos + 1] == '*')
			{
				const int startLine = line;
				pos += 2;
				while (pos + 1 < length && !(text[pos] == '*' && text[pos + 1] == '/'))
				{
					if (text[pos] == '\n') line++;
					pos++;
				}
				if (pos + 1 >= length)
				{
					tok->kind = kTokError;
					tok->text = "Unterminated comment";
					tok->line = startLine;
					return false;
				}
				pos += 2;
			}
			else break;
		}
		return true;
	}

	Token Next()
	{
		Token tok;
		tok.kind = kTokEnd;
		tok.number = 0;
		if (!SkipSpaceAndComments(&tok)) return tok;
		tok.line = line;
		if (pos >= length) return tok;

		const char c = text[pos];
		if (std::isalpha((unsigned char)c) || c == '_')
		{
			const size_t begin = pos;
			while (pos < length && (std::isalnum((unsigned char)text[pos]) || text[pos] == '_')) pos++;
			tok.kind = kTokIdentifier;
			tok.text.assign(text + begin, pos - begin);
		}
		else if (std::isdigit((unsigned char)c))
		{
			const size_t begin = pos;
			while (pos < length && (std::isalnum((unsigned char)text[pos]) || text[pos] == '_')) pos++;
			tok.kind = kTokInt;
			if (pos < length && text[pos] == '.')
			{
				pos++;
				while (pos < length && std::isdigit((unsigned char)text[pos])) pos++;
				tok.kind = kTokFloat;
			}
			tok.text.assign(text + begin, pos - begin);
			if (tok.kind == kTokInt)
			{
				char *stop;
				tok.number = std::strtol(tok.text.c_str(), &stop, 0);
				if (*stop != 0)
				{
					tok.kind = kTokError;
					tok.text = "Bad numeric constant '" + tok.text + "'";
				}
			}
		}
		else if (c == '"')
		{
			pos++;
			while (pos < length && text[pos] != '"')
			{
				char ch = text[pos++];
				if (ch == '\n') line++;
				else if (ch == '\\' && pos < length)
				{
					const char esc = text[pos++];
					if (esc == '\n') line++;
					if (esc == '"' || esc == '\\') ch = esc;
					else if (esc == 'n') ch = '\n';
					else { tok.text += '\\'; ch = esc; }
				}
				tok.text += ch;
			}
			if (pos >= length)
			{
				tok.kind = kTokError;
				tok.text = "Unterminated string";
				return tok;
			}
			pos++;
			tok.kind = kTokString;
		}
		else
		{
			pos++;
			tok.kind = kTokPunct;
			tok.text.assign(1, c);
		}
		return tok;
	}

	Token Peek() const
	{
		Lexer copy = *this;
		return copy.Next();
	}
};

bool IsPunct(const Token &tok, char c)
{
	return tok.kind == kTokPunct && tok.text[0] == c;
}

// [Doruk-Aksoy] FScanner's wording for a token that is not the one the grammar needs.
std::string Expected(const char *what, const Token &tok)
{
	if (tok.kind == kTokEnd) return std::string("Expected ") + what + " but reached the end of the lump";
	return std::string("Expected ") + what + " but got '" + tok.text + "' instead";
}

bool EqualsNoCase(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
	}
	return *a == *b;
}

uint32_t ReadBigEndian32(const unsigned char *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// [Doruk-Aksoy] The colours of a PNG's PLTE chunk, or -1 when IDAT or IEND (or the end) comes first.
int ReadPngPalette(const unsigned char *data, size_t size, unsigned char *out, std::string *error)
{
	size_t pos = 8;
	while (pos + 8 <= size)
	{
		const uint32_t len = ReadBigEndian32(data + pos);
		const unsigned char *id = data + pos + 4;
		if (std::memcmp(id, "PLTE", 4) == 0)
		{
			size_t bytes = size - (pos + 8);
			if (bytes > len) bytes = len;
			if (bytes > size_t(kPaletteColors * 3)) bytes = size_t(kPaletteColors * 3);
			std::memcpy(out, data + pos + 8, bytes);
			return int(bytes / 3);
		}
		if (std::memcmp(id, "IDAT", 4) == 0 || std::memcmp(id, "IEND", 4) == 0) break;
		if (len > size - (pos + 8)) break;
		pos += 8 + size_t(len) + 4;
	}
	*error = "contains no palette";
	return -1;
}

// [Doruk-Aksoy] A JASC-PAL text: the header word, a version, a colour count, then three 0-255 numbers per colour.
int ReadJascPalette(const unsigned char *data, size_t size, unsigned char *out, std::string *error)
{
	size_t pos = 8;
	long numbers[2];
	int have = 0;
	int colors = 0;
	int filled = 0;
	for (;;)
	{
		while (pos < size && std::isspace(data[pos])) pos++;
		if (have == 2 && filled >= colors) return colors / 3;
		if (pos >= size)
		{
			*error = "Missing integer (unexpected end of file)";
			return -1;
		}
		const size_t begin = pos;
		while (pos < size && !std::isspace(data[pos])) pos++;
		const std::string word(reinterpret_cast<const char *>(data) + begin, pos - begin);
		char *stop;
		const long value = std::strtol(word.c_str(), &stop, 0);
		if (*stop != 0)
		{
			*error = "Bad numeric constant '" + word + "'";
			return -1;
		}
		if (have < 2)
		{
			numbers[have++] = value;
			if (have == 2)
			{
				const long count = numbers[1] < 0 ? 0 : (numbers[1] > kPaletteColors ? kPaletteColors : numbers[1]);
				colors = int(count) * 3;
			}
			continue;
		}
		if (value < 0 || value > 255)
		{
			*error = "Color " + word + " value out of range";
			return -1;
		}
		out[filled++] = (unsigned char)value;
	}
}

} // namespace

TrnslateParse ComputeParseTrnslate(const char *text, size_t length,
	const std::function<bool(const std::string &)> &isPaletteLump)
{
	TrnslateParse out;
	out.errorLine = 0;
	Lexer lex = { text, length, 0, 1 };

	auto fail = [&out](const Token &tok, const std::string &message) {
		out.error = message;
		out.errorLine = tok.line;
		return out;
	};

	for (;;)
	{
		Token tok = lex.Next();
		if (tok.kind == kTokEnd) return out;
		if (tok.kind == kTokError) return fail(tok, tok.text);
		if (tok.kind != kTokIdentifier) return fail(tok, Expected("a translation name", tok));

		TrnslateDef def;
		def.name = tok.text;
		def.line = tok.line;
		def.baseKind = kBaseIdentity;
		def.baseStandard = 0;

		tok = lex.Next();
		if (IsPunct(tok, ':'))
		{
			tok = lex.Next();
			if (tok.kind == kTokInt)
			{
				if (tok.number < 0 || tok.number > kMaxStandardTranslation)
				{
					return fail(tok, "Translation must be in the range [0," + std::to_string(kMaxStandardTranslation) + "]");
				}
				def.baseKind = kBaseStandard;
				def.baseStandard = int(tok.number);
			}
			else if (tok.kind == kTokIdentifier)
			{
				def.baseKind = kBaseNamed;
				def.baseName = tok.text;
			}
			else if (tok.kind == kTokError) return fail(tok, tok.text);
			else return fail(tok, Expected("a base translation", tok));
			tok = lex.Next();
		}
		if (tok.kind == kTokError) return fail(tok, tok.text);
		if (!IsPunct(tok, '=')) return fail(tok, Expected("'='", tok));

		for (;;)
		{
			tok = lex.Next();
			if (tok.kind == kTokError) return fail(tok, tok.text);
			if (tok.kind != kTokString) return fail(tok, Expected("a string", tok));

			TrnslateEntry entry;
			entry.text = tok.text;
			entry.palette = isPaletteLump(entry.text);
			entry.start = 0;
			if (entry.palette && IsPunct(lex.Peek(), ','))
			{
				lex.Next();
				tok = lex.Next();
				bool negative = false;
				if (IsPunct(tok, '-'))
				{
					negative = true;
					tok = lex.Next();
				}
				if (tok.kind == kTokError) return fail(tok, tok.text);
				if (tok.kind != kTokInt) return fail(tok, Expected("a number", tok));
				entry.start = int(negative ? -tok.number : tok.number);
			}
			def.entries.push_back(entry);
			if (!IsPunct(lex.Peek(), ',')) break;
			lex.Next();
		}

		out.defs.push_back(def);
	}
}

TrnslateBuiltin ComputeTrnslateBuiltin(const char *name, int *player)
{
	if (EqualsNoCase(name, "None")) return kBuiltinNone;
	if (EqualsNoCase(name, "Ice")) return kBuiltinIce;
	if (std::strlen(name) == 7 && EqualsNoCase(std::string(name, 6).c_str(), "Player") && name[6] >= '1' && name[6] <= '8')
	{
		*player = name[6] - '1';
		return kBuiltinPlayer;
	}
	return kNotBuiltin;
}

int ComputeReadPaletteLump(const unsigned char *data, size_t size, unsigned char out[kPaletteColors * 3],
	std::string *error)
{
	static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::memset(out, 0, kPaletteColors * 3);
	if (size >= 8 && std::memcmp(data, pngSignature, 8) == 0) return ReadPngPalette(data, size, out, error);
	if (size >= 8 && std::memcmp(data, "JASC-PAL", 8) == 0) return ReadJascPalette(data, size, out, error);
	if (size > 0) std::memcpy(out, data, size < size_t(kPaletteColors * 3) ? size : size_t(kPaletteColors * 3));
	return kPaletteColors;
}

bool ComputePaletteSpanFits(int start, int count)
{
	const int last = start + count - 1;
	return start >= 0 && start <= 255 && last >= 0 && last <= 255;
}

SetTranslationOutcome ComputeSetTranslationOutcome(bool nameEmpty, bool found)
{
	if (nameEmpty) return kSetTranslationDefault;
	return found ? kSetTranslationFound : kSetTranslationKeep;
}

}} // namespace zx::trnslate

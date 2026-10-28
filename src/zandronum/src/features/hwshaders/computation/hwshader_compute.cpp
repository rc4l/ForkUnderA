// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/computation/hwshader_compute.h"

#include <ctype.h>
#include <stdio.h>
#include <initializer_list>
#include <utility>
#include <vector>

namespace zx { namespace hwshaders {

int ComputePieceShader(int warp, int userShader)
{
	if (warp == 1 || warp == 2) return warp;
	return userShader >= 0 ? 3 + userShader : 0;
}

int ComputeUserShaderOf(int pieceShader)
{
	return pieceShader >= 3 ? pieceShader - 3 : -1;
}

int ComputeDrawTextureShader(int materialShader)
{
	return materialShader >= kFirstUserShader ? -1 : 0;
}

namespace {

bool IdentChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// [Doruk-Aksoy] Whether s[at, at + len) stands as a whole word.
bool WholeAt(const std::string &s, size_t at, size_t len)
{
	return (at == 0 || !IdentChar(s[at - 1])) && (at + len >= s.size() || !IdentChar(s[at + len]));
}

// [rc4l] s with every whole-word `word` replaced by `with`.
std::string ReplaceWord(const std::string &s, const std::string &word, const std::string &with)
{
	std::string out;
	size_t i = 0;
	while (i < s.size())
	{
		const size_t at = s.find(word, i);
		if (at == std::string::npos) { out.append(s, i, std::string::npos); break; }
		const size_t end = at + word.size();
		out.append(s, i, at - i);
		out += WholeAt(s, at, word.size()) ? with : word;
		i = end;
	}
	return out;
}

// [rc4l] s with every `what` replaced by `with`, whatever surrounds it.
std::string ReplaceAll(const std::string &s, const std::string &what, const std::string &with)
{
	std::string out;
	size_t i = 0;
	for (size_t at; (at = s.find(what, i)) != std::string::npos; i = at + what.size())
		out.append(s, i, at - i) += with;
	return out.append(s, i, std::string::npos);
}

// [Doruk-Aksoy] Whether line, its spaces taken out, is one of `decls`.
bool IsDeclaration(const std::string &line, std::initializer_list<const char *> decls)
{
	std::string t;
	for (char c : line) if (!isspace((unsigned char)c)) t += c;
	for (const char *d : decls) if (t == d) return true;
	return false;
}

// [Doruk-Aksoy] source without the lines `drop` picks, each line kept ending in a newline.
template <class Drop> std::string KeepLines(const std::string &source, Drop drop)
{
	std::string kept;
	size_t i = 0;
	while (i < source.size())
	{
		size_t end = source.find('\n', i);
		if (end == std::string::npos) end = source.size();
		const std::string line = source.substr(i, end - i);
		if (!drop(line)) kept += line + "\n";
		i = end + 1;
	}
	return kept;
}

// [Doruk-Aksoy] Whether `word` stands whole somewhere in s.
bool HasWord(const std::string &s, const std::string &word)
{
	for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1))
		if (WholeAt(s, at, word.size())) return true;
	return false;
}

// [Doruk-Aksoy] Where the spaces and tabs before s[at] begin.
size_t BackOverSpaces(const std::string &s, size_t at)
{
	while (at > 0 && (s[at - 1] == ' ' || s[at - 1] == '\t')) at--;
	return at;
}

// [Doruk-Aksoy] Whether s has `name` of its own: a `#define name` line, or a float or vector declared under it.
bool OwnsName(const std::string &s, const std::string &name)
{
	for (size_t at = s.find(name); at != std::string::npos; at = s.find(name, at + 1))
	{
		if (!WholeAt(s, at, name.size())) continue;
		const size_t b = BackOverSpaces(s, at);
		size_t a = b;
		while (a > 0 && IdentChar(s[a - 1])) a--;
		const std::string before = s.substr(a, b - a);
		const size_t hash = BackOverSpaces(s, a);
		if ((before == "define" && hash > 0 && s[hash - 1] == '#') || before == "float" || before == "vec2" || before == "vec3" || before == "vec4")
			return true;
	}
	return false;
}

} // namespace

std::string ComputeRewriteIndexedName(const std::string &source, const std::string &name, const std::string &func)
{
	std::string out;
	size_t i = 0;
	for (size_t at; (at = source.find(name, i)) != std::string::npos;)
	{
		const size_t end = at + name.size();
		const size_t open = source.find_first_not_of(" \t", end);
		size_t close = source.size();
		if (WholeAt(source, at, name.size()) && open != std::string::npos && source[open] == '[')
		{
			int depth = 0;
			for (close = open; close < source.size(); close++)
			{
				if (source[close] == '[') depth++;
				else if (source[close] == ']' && --depth == 0) break;
			}
		}
		// [Doruk-Aksoy] Not an index, or one never closed, is left as it is.
		if (close >= source.size())
		{
			out.append(source, i, end - i);
			i = end;
			continue;
		}
		out.append(source, i, at - i);
		out += func + "(" + ComputeRewriteIndexedName(source.substr(open + 1, close - open - 1), name, func) + ")";
		i = close + 1;
	}
	return out.append(source, i, std::string::npos);
}

std::string ComputeLegacyShaderNames(const std::string &source)
{
	const std::string kept = KeepLines(source, [](const std::string &line) {
		return IsDeclaration(line, { "uniformvec3camerapos;", "varyingvec4pixelpos;", "invec4pixelpos;" });
	});
	const std::string rewritten = ComputeRewriteIndexedName(kept, "gl_TextureMatrix", "fuaTextureMatrix");
	std::string pre;
	if (HasWord(rewritten, "camerapos") && !OwnsName(rewritten, "camerapos"))
		pre += "#define camerapos (uCameraPos.xyz)\n";
	if (rewritten != kept)
		pre += "mat4 fuaTextureMatrix(int i) { return i == 7 ? ModelMatrix : (i == 0 ? TextureMatrix : mat4(1.0)); }\n";
	return pre + rewritten;
}

bool ComputeAdaptUserShader(const std::string &source, int index, std::string &out)
{
	std::string kept = ComputeLegacyShaderNames(KeepLines(source, [](const std::string &line) {
		const size_t first = line.find_first_not_of(" \t\r");
		return (first != std::string::npos && line.compare(first, 8, "#version") == 0) || IsDeclaration(line, { "uniformfloattimer;" });
	}));
	const bool newStyle = kept.find("ProcessTexel") != std::string::npos;
	const std::string n = std::to_string(index);
	kept = ReplaceAll(kept, "gl_TexCoord[0]", "vTexCoord");
	kept = ReplaceWord(kept, "ProcessTexel", "fuaProcessTexel_" + n);
	kept = ReplaceWord(kept, "ProcessLight", "fuaProcessLight_" + n);
	kept = ReplaceWord(kept, "Process", "fuaProcess_" + n);
	out = kept;
	return newStyle;
}

namespace {

// [Doruk-Aksoy] What one kind of program hands a GLDEFS shader, and how its entry calls it; an `@` in newCall is the ProcessTexel call.
struct ProgramNames
{
	const char *marker;
	const char *inputs;
	const char *getTexel, *sample;
	std::vector<std::pair<const char *, const char *>> names;
	const char *entry, *guard, *timer, *newCall, *oldColor;
	const char *alphaNew, *alphaOld;   // [Doruk-Aksoy] fuaSurfaceAlpha's value, or null for a kind without one
	const char *plain;
};

const ProgramNames &NamesOf(ProgramKind kind)
{
	// [Doruk-Aksoy] Locations 23 to 26 are the model matrix's columns, which the scene vertex shaders write under FUA_USER_SHADER.
	static const ProgramNames kScene = {
		"//FUA_USER_SHADERS",
		"layout(location = 23) flat in vec4 vModel0;\n"
		"layout(location = 24) flat in vec4 vModel1;\n"
		"layout(location = 25) flat in vec4 vModel2;\n"
		"layout(location = 26) flat in vec4 vModel3;\n",
		"fuaTexMode(fuaTexel(st))", "fuaTexel",
		{ { "pixelpos", "vPixelPos" }, { "glowdist", "vGlowDist" }, { "uCameraPos", "vec4(uCameraPos.xyz, 0.0)" },
		  { "vColor", "vec4(vColor, vLightParm.z)" }, { "uDesaturationFactor", "vDesaturation" },
		  { "ModelMatrix", "mat4(vModel0, vModel1, vModel2, vModel3)" }, { "TextureMatrix", "mat4(1.0)" } },
		"vec4 fuaSurfaceTexel(vec2 fuaUV) {\n",
		"    if (vWarp.x < 2.5) return fuaTexMode(fuaTexel(fuaUV));\n",
		"uFrame.x * vWarp.y", "@", "vec4(1.0, 1.0, 1.0, vLightParm.z)",
		"vLightParm.z", "vWarp.x < 2.5 ? vLightParm.z : 1.0",
		"vec4 fuaSurfaceTexel(vec2 fuaUV) { return fuaTexMode(fuaTexel(fuaUV)); }\n"
		"float fuaSurfaceAlpha() { return vLightParm.z; }\n",
	};
	// [Doruk-Aksoy] The 2D layer's own vec4 vColor is GL's, so it is not renamed; a 2D draw has no world to place.
	static const ProgramNames k2D = {
		"//FUA_2D_SHADERS",
		"",
		"fua2DTexel(st)", "fua2DRaw",
		{ { "pixelpos", "vec4(gl_FragCoord.xy, 0.0, 0.0)" }, { "glowdist", "vec2(0.0)" }, { "uCameraPos", "vec4(0.0)" },
		  { "uDesaturationFactor", "0.0" }, { "ModelMatrix", "mat4(1.0)" }, { "TextureMatrix", "mat4(1.0)" } },
		"vec4 fua2DColor(vec2 fuaUV) {\n",
		"",
		"uInvScreen.z * vSpeed", "@ * vColor", "vColor",
		nullptr, nullptr,
		"vec4 fua2DColor(vec2 fuaUV) { return fua2DTexel(fuaUV) * vColor; }\n",
	};
	return kind == k2DProgram ? k2D : kScene;
}

} // namespace

const char *ComputeProgramMarker(ProgramKind kind)
{
	return NamesOf(kind).marker;
}

std::string ComputeUserProgramBlock(ProgramKind kind, const std::string &adapted, bool newStyle, int index)
{
	const ProgramNames &p = NamesOf(kind);
	const std::string n = std::to_string(index);
	std::string b = std::string(p.inputs) +
		"vec4 vTexCoord;\n"
		"float timer;\n"
		"vec4 getTexel(vec2 st) { return " + p.getTexel + "; }\n"
		"#define texture2D(s, uv) " + p.sample + "(uv)\n"
		"#define texture(s, uv) " + p.sample + "(uv)\n";
	for (const auto &name : p.names) b += std::string("#define ") + name.first + " " + name.second + "\n";
	b += adapted + "#undef texture2D\n#undef texture\n";
	for (const auto &name : p.names) b += std::string("#undef ") + name.first + "\n";
	// [Doruk-Aksoy] ProcessTexel is GZDoom's, its colour applied after it; Process(color) is Zandronum 3.3's, handed the colour with the draw's alpha and its own alpha kept.
	b += std::string(p.entry) + p.guard +
		"    vTexCoord = vec4(fuaUV, 0.0, 1.0);\n"
		"    timer = " + p.timer + ";\n"
		"    return " + (newStyle ? ReplaceAll(p.newCall, "@", "fuaProcessTexel_" + n + "()") : "fuaProcess_" + n + "(" + p.oldColor + ")") + ";\n"
		"}\n";
	if (p.alphaNew != nullptr) b += std::string("float fuaSurfaceAlpha() { return ") + (newStyle ? p.alphaNew : p.alphaOld) + "; }\n";
	return b;
}

std::string ComputePlainProgramBlock(ProgramKind kind)
{
	return NamesOf(kind).plain;
}

std::string ComputeSpliceShaderBlock(const std::string &source, const std::string &marker, const std::string &block)
{
	const size_t at = source.find(marker);
	if (at == std::string::npos) return source;
	const size_t lineStart = source.rfind('\n', at);
	const size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
	size_t to = source.find('\n', at);
	to = to == std::string::npos ? source.size() : to + 1;
	return source.substr(0, from) + block + source.substr(to);
}

std::string ComputePrecompileNote(int shaders, int programs, int leftOut, double ms)
{
	char buf[160];
	snprintf(buf, sizeof(buf), "vulkan: %d GLDEFS shader%s compiled into %d program%s in %.0f ms", shaders, shaders == 1 ? "" : "s",
		programs, programs == 1 ? "" : "s", ms);
	std::string note = buf;
	if (leftOut > 0) note += ", " + std::to_string(leftOut) + " left out";
	return note + "\n";
}

std::string ComputeIndentedLines(const std::string &text)
{
	std::string out;
	size_t from = 0;
	while (from < text.size())
	{
		size_t to = text.find('\n', from);
		if (to == std::string::npos) to = text.size();
		if (to > from) out += "  " + text.substr(from, to - from) + "\n";
		from = to + 1;
	}
	return out;
}

std::string ComputeUserShaderSkipNote(const std::string &renderer, const std::string &lump, const std::string &why)
{
	return renderer + ": hardware shader " + lump + " does not compile here; its textures draw without it\n" + ComputeIndentedLines(why);
}

}} // namespace zx::hwshaders

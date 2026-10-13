// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/computation/hwshader_compute.h"

#include <ctype.h>

namespace zx { namespace hwshaders {

int ComputePieceShader(int warp, int userShader)
{
	if (warp == 1 || warp == 2) return warp;
	return userShader >= 0 ? 3 + userShader : 0;
}

namespace {

bool IdentChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

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
		const bool whole = (at == 0 || !IdentChar(s[at - 1])) && (end >= s.size() || !IdentChar(s[end]));
		out.append(s, i, at - i);
		out += whole ? with : word;
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

// [rc4l] Whether line is `uniform float timer;` give or take spaces.
bool IsTimerUniform(const std::string &line)
{
	std::string t;
	for (char c : line) if (!isspace((unsigned char)c)) t += c;
	return t == "uniformfloattimer;";
}

} // namespace

bool ComputeAdaptUserShader(const std::string &source, int index, std::string &out)
{
	std::string kept;
	size_t i = 0;
	while (i < source.size())
	{
		size_t end = source.find('\n', i);
		if (end == std::string::npos) end = source.size();
		const std::string line = source.substr(i, end - i);
		size_t first = line.find_first_not_of(" \t\r");
		const bool version = first != std::string::npos && line.compare(first, 8, "#version") == 0;
		if (!version && !IsTimerUniform(line)) kept += line + "\n";
		i = end + 1;
	}
	const bool newStyle = kept.find("ProcessTexel") != std::string::npos;
	const std::string n = std::to_string(index);
	kept = ReplaceAll(kept, "gl_TexCoord[0]", "vTexCoord");
	kept = ReplaceWord(kept, "ProcessTexel", "fuaProcessTexel_" + n);
	kept = ReplaceWord(kept, "ProcessLight", "fuaProcessLight_" + n);
	kept = ReplaceWord(kept, "Process", "fuaProcess_" + n);
	out = kept;
	return newStyle;
}

std::string ComputeUserShaderBlock(const std::vector<std::string> &adapted, const std::vector<bool> &newStyle)
{
	std::string b =
		"vec4 vTexCoord;\n"
		"float timer;\n"
		"vec4 getTexel(vec2 st) { return fuaTexMode(fuaTexel(st)); }\n"
		"#define texture2D(s, uv) fuaTexel(uv)\n";
	for (const std::string &a : adapted) b += a;
	b += "#undef texture2D\n"
		"vec4 fuaUserTexel(int which, vec2 uv) {\n"
		"    vTexCoord = vec4(uv, 0.0, 1.0);\n"
		"    timer = uFrame.x * vWarp.y;\n";
	for (size_t k = 0; k < adapted.size(); k++)
	{
		if (adapted[k].empty()) continue;
		const std::string n = std::to_string(k);
		b += "    if (which == " + n + ") return " + (k < newStyle.size() && newStyle[k] ? "fuaProcessTexel_" + n + "()" : "fuaProcess_" + n + "(vec4(1.0))") + ";\n";
	}
	b += "    return fuaTexMode(fuaTexel(uv));\n"
		"}\n"
		"vec4 fuaSurfaceTexel(vec2 uv) {\n"
		"    if (vWarp.x > 2.5) return fuaUserTexel(int(vWarp.x + 0.5) - 3, uv);\n"
		"    return fuaTexMode(fuaTexel(uv));\n"
		"}\n";
	return b;
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

}} // namespace zx::hwshaders

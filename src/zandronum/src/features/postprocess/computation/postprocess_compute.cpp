// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/postprocess/computation/postprocess_compute.h"
#include "features/hwshaders/computation/hwshader_compute.h"   // [Doruk-Aksoy] ComputeIndentedLines

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace zx { namespace postprocess {

namespace {

std::string Lower(const std::string &s)
{
	std::string out(s);
	for (char &c : out) c = (char)std::tolower((unsigned char)c);
	return out;
}

bool SameName(const std::string &a, const std::string &b) { return Lower(a) == Lower(b); }

// [Doruk-Aksoy] A GLDEFS token: a word, a brace, or a quoted string's text, and where it ends.
struct Token
{
	std::string text;
	bool quoted;
	size_t end;
};

bool CommentAt(const std::string &s, size_t i) { return s.compare(i, 2, "//") == 0 || s.compare(i, 2, "/*") == 0; }

// [Doruk-Aksoy] A GLDEFS lump's tokens, its comments left out, as FScanner reads them.
std::vector<Token> Tokens(const std::string &s)
{
	std::vector<Token> out;
	size_t i = 0;
	while (i < s.size())
	{
		if (std::isspace((unsigned char)s[i])) { i++; continue; }
		if (s.compare(i, 2, "//") == 0) { i = std::min(s.size(), s.find('\n', i)); continue; }
		if (s.compare(i, 2, "/*") == 0)
		{
			const size_t close = s.find("*/", i + 2);
			i = close == std::string::npos ? s.size() : close + 2;
			continue;
		}
		Token t;
		t.quoted = s[i] == '"';
		if (t.quoted)
		{
			const size_t from = ++i;
			while (i < s.size() && s[i] != '"') i += (s[i] == '\\' && i + 1 < s.size()) ? 2 : 1;
			t.text = s.substr(from, std::min(i, s.size()) - from);
			i = std::min(i + 1, s.size());
		}
		else if (s[i] == '{' || s[i] == '}') t.text = std::string(1, s[i++]);
		else
		{
			const size_t from = i;
			while (i < s.size() && !std::isspace((unsigned char)s[i]) && s[i] != '{' && s[i] != '}' && s[i] != '"' && !CommentAt(s, i)) i++;
			t.text = s.substr(from, i - from);
		}
		t.end = i;
		out.push_back(t);
	}
	return out;
}

const char *GlslType(UniformType type)
{
	switch (type)
	{
	case kUniformInt: return "int";
	case kUniformFloat: return "float";
	case kUniformVec2: return "vec2";
	case kUniformVec3: return "vec3";
	default: return "vec4";
	}
}

// [Doruk-Aksoy] A level's entry for (shader, player), made when `make` and it has none.
ShaderEntry *EntryFor(ShaderTable &table, const std::string &shader, int player, bool make)
{
	for (ShaderEntry &e : table.entries)
		if (e.player == player && SameName(e.shader, shader)) return &e;
	if (!make) return nullptr;
	table.entries.emplace_back();
	table.entries.back().shader = shader;
	table.entries.back().player = player;
	return &table.entries.back();
}

const ShaderEntry *FindEntry(const ShaderTable &table, const std::string &shader, int player)
{
	for (const ShaderEntry &e : table.entries)
		if (e.player == player && SameName(e.shader, shader)) return &e;
	return nullptr;
}

bool Expired(const ShaderEntry &e, int now) { return e.enabled == 1 && e.expiresTic > 0 && now >= e.expiresTic; }

const double *UniformOf(const ShaderEntry *entry, const std::string &uniform)
{
	if (entry == nullptr) return nullptr;
	for (const ShaderEntry::Uniform &u : entry->uniforms)
		if (u.name == uniform) return u.values;
	return nullptr;
}

} // namespace

const char *const kInputName = "fuaInput";
const char *const kLastInputName = "fuaLastInput";
const char *const kAutoBlockName = "fuaPostAuto";
const char *const kUniformBlockName = "Uniforms";

int ComputeStage(const std::string &target)
{
	const std::string t = Lower(target);
	if (t == "beforebloom") return kStageBeforeBloom;
	if (t == "scene") return kStageScene;
	if (t == "screen") return kStageScreen;
	return -1;
}

UniformType ComputeUniformType(const std::string &word)
{
	const std::string w = Lower(word);
	if (w == "int") return kUniformInt;
	if (w == "float") return kUniformFloat;
	if (w == "vec2") return kUniformVec2;
	if (w == "vec3") return kUniformVec3;
	if (w == "vec4") return kUniformVec4;
	return kUniformUndefined;
}

int ComputeUniformComponents(UniformType type)
{
	switch (type)
	{
	case kUniformInt: case kUniformFloat: return 1;
	case kUniformVec2: return 2;
	case kUniformVec3: return 3;
	case kUniformVec4: return 4;
	default: return 0;
	}
}

bool ComputeCvarUniformFits(CvarKind kind, UniformType type)
{
	if (kind == kCvarInt || kind == kCvarFloat) return type == kUniformInt || type == kUniformFloat;
	if (kind == kCvarColor) return type == kUniformVec3 || type == kUniformVec4;
	return false;
}

void ComputeColorUniform(unsigned argb, double out[4])
{
	out[0] = ((argb >> 16) & 0xff) / 255.0;
	out[1] = ((argb >> 8) & 0xff) / 255.0;
	out[2] = (argb & 0xff) / 255.0;
	out[3] = ((argb >> 24) & 0xff) / 255.0;
}

bool ComputeShaderVersionValid(int version) { return version >= 330 && version <= 450; }

unsigned ComputeUniformLayout(std::vector<UniformField> &fields)
{
	unsigned offset = 0;
	for (UniformField &f : fields)
	{
		const unsigned components = (unsigned)ComputeUniformComponents(f.type);
		// [Doruk-Aksoy] std140: a scalar on 4 bytes, a vec2 on 8, a vec3 and vec4 on 16.
		const unsigned align = components <= 1 ? 4u : components == 2 ? 8u : 16u;
		offset = (offset + align - 1) / align * align;
		f.offset = offset;
		offset += 4u * std::max(components, 1u);
	}
	return std::max(16u, (offset + 15u) / 16u * 16u);
}

void ComputeWriteUniform(unsigned char *block, const UniformField &field, const double values[4])
{
	if (field.type == kUniformInt)
	{
		const int v = (int)values[0];
		memcpy(block + field.offset, &v, sizeof(v));
		return;
	}
	const int n = ComputeUniformComponents(field.type);
	for (int i = 0; i < n; i++)
	{
		const float v = (float)values[i];
		memcpy(block + field.offset + 4 * i, &v, sizeof(v));
	}
}

std::string ComputeTextureVariable(const std::string &name) { return "fuaTex_" + name; }

std::vector<LayoutTexture> ComputeLayoutTextures(bool lastInput, const std::vector<std::string> &textureNames, const std::vector<std::string> &present)
{
	std::vector<LayoutTexture> wanted = { { kInputName, true } };
	if (lastInput) wanted.push_back({ kLastInputName, true });
	for (const std::string &t : textureNames) wanted.push_back({ ComputeTextureVariable(t), false });
	std::vector<LayoutTexture> out;
	for (const LayoutTexture &w : wanted)
		if (std::find(present.begin(), present.end(), w.name) != present.end()) out.push_back(w);
	return out;
}

std::string ComputePostProcessProlog(const ProgramDesc &desc)
{
	std::ostringstream s;
	s << "#version 450\n"
	     "layout(location = 0) in vec2 vUV;\n"
	     "layout(location = 0) out vec4 FragColor;\n"
	     // [Doruk-Aksoy] The region vectors hold the share of the target a picture covers (xy) and its size (zw).
	     "layout(binding = 0) uniform " << kAutoBlockName << "\n{\n"
	     "    float InputTimeDelta;\n    float InputTime;\n    float InputTimeGame;\n    float fua_EnabledTime;\n"
	     "    vec4 fuaPostRegion;\n    vec4 fuaLastRegion;\n    vec4 fuaPostOutput;\n};\n";
	if (!desc.uniforms.empty())
	{
		s << "layout(binding = 1) uniform " << kUniformBlockName << "\n{\n";
		for (const UniformField &f : desc.uniforms)
			s << "    layout(offset = " << f.offset << ") " << GlslType(f.type) << " " << f.name << ";\n";
		s << "};\n";
	}
	s << "layout(binding = 2) uniform texture2D " << kInputName << "; uniform sampler " << kInputName << "_sampler;\n";
	if (desc.lastInput)
		s << "layout(binding = 3) uniform texture2D " << kLastInputName << "; uniform sampler " << kLastInputName << "_sampler;\n";
	int binding = 4;
	for (const std::string &t : desc.textureNames)
	{
		const std::string v = ComputeTextureVariable(t);
		s << "layout(binding = " << binding++ << ") uniform texture2D " << v << "; uniform sampler " << v << "_sampler;\n"
		  << "#define " << t << " sampler2D(" << v << ", " << v << "_sampler)\n";
	}
	// [Doruk-Aksoy] GL's screen has y up, so TexCoord and gl_FragCoord count from the bottom, gl_FragCoord in the input's pixels whatever size the pass draws at, and the pictures are read flipped back.
	s << "vec2 fuaTexCoord() { return vec2(vUV.x, 1.0 - vUV.y); }\n"
	     "#define TexCoord fuaTexCoord()\n"
	     "vec4 fuaFragCoord()\n{\n"
	     "    vec2 s = fuaPostRegion.zw / fuaPostOutput.xy;\n"
	     "    return vec4(gl_FragCoord.x * s.x, (fuaPostOutput.y - gl_FragCoord.y) * s.y, gl_FragCoord.zw);\n}\n"
	     "#define gl_FragCoord fuaFragCoord()\n"
	     "struct FuaPostPicture { int which; };\n"
	     "vec4 fuaRegionOf(FuaPostPicture p) { return p.which == 0 ? fuaPostRegion : fuaLastRegion; }\n"
	     "vec2 fuaPictureUV(FuaPostPicture p, vec2 uv)\n{\n"
	     "    vec4 r = fuaRegionOf(p);\n"
	     "    return min(vec2(uv.x, 1.0 - uv.y) * r.xy, r.xy - 0.5 * r.xy / r.zw);\n}\n";
	const std::string input = std::string("sampler2D(") + kInputName + ", " + kInputName + "_sampler)";
	const std::string last = desc.lastInput ? std::string("sampler2D(") + kLastInputName + ", " + kLastInputName + "_sampler)" : input;
	// [Doruk-Aksoy] Each read of a picture goes to the input or the previous frame by which one the mod named.
	auto pick = [&](const std::string &call) {
		std::string a = call, b = call;
		a.replace(a.find("$"), 1, input);
		b.replace(b.find("$"), 1, last);
		return "p.which == 0 ? " + a + " : " + b;
	};
	s << "vec4 texture(FuaPostPicture p, vec2 uv) { return " << pick("texture($, fuaPictureUV(p, uv))") << "; }\n"
	  << "vec4 texture(FuaPostPicture p, vec2 uv, float bias) { return " << pick("texture($, fuaPictureUV(p, uv), bias)") << "; }\n"
	  << "vec4 textureLod(FuaPostPicture p, vec2 uv, float lod) { return " << pick("textureLod($, fuaPictureUV(p, uv), lod)") << "; }\n"
	  << "vec4 textureOffset(FuaPostPicture p, vec2 uv, ivec2 off) { return texture(p, uv + vec2(off) / fuaRegionOf(p).zw); }\n"
	  << "ivec2 textureSize(FuaPostPicture p, int lod) { return ivec2(fuaRegionOf(p).zw); }\n"
	  << "vec4 texelFetch(FuaPostPicture p, ivec2 c, int lod)\n{\n"
	     "    ivec2 size = ivec2(fuaRegionOf(p).zw);\n"
	     "    ivec2 at = clamp(ivec2(c.x, size.y - 1 - c.y), ivec2(0), size - 1);\n"
	     "    return " << pick("texelFetch($, at, 0)") << ";\n}\n"
	  << "#define InputTexture FuaPostPicture(0)\n"
	  << "#define LastInputTexture FuaPostPicture(1)\n";
	return s.str();
}

std::string ComputeAdaptPostProcessShader(const std::string &source)
{
	std::istringstream in(source);
	std::string out, line;
	while (std::getline(in, line))
	{
		const size_t first = line.find_first_not_of(" \t");
		if (first != std::string::npos && line.compare(first, 8, "#version") == 0) continue;
		out += line;
		out += '\n';
	}
	return out;
}

std::string ComputePostShaderSkipNote(const std::string &renderer, const std::string &name, const std::string &why)
{
	return renderer + ": post-process shader " + name + " does not compile here; it is left out\n" + zx::hwshaders::ComputeIndentedLines(why);
}

bool ComputeReadsLastInput(const std::string &source) { return source.find("LastInputTexture") != std::string::npos; }

bool ComputeSetShaderEnabled(ShaderTable &table, const std::string &shader, int player, bool enabled, int now, int expires, int since)
{
	ShaderEntry *e = EntryFor(table, shader, player, true);
	const int state = enabled ? 1 : 0;
	const int until = enabled ? expires : 0;
	// [Doruk-Aksoy] Asked again for what it already is, untimed, nothing changes; a timed call always moves the end.
	if (e->enabled == state && e->expiresTic == 0 && until == 0) return false;
	// [Doruk-Aksoy] A shader turned on again while it runs keeps its clock, its end moving to the new one.
	if (enabled && (e->enabled != 1 || Expired(*e, now))) e->enabledTic = since == kSinceNow ? now : since;
	e->enabled = state;
	e->expiresTic = until;
	table.generation++;
	return true;
}

bool ComputeSetShaderUniform(ShaderTable &table, const std::string &shader, int player, const std::string &uniform, const double values[4], int kind)
{
	ShaderEntry *e = EntryFor(table, shader, player, true);
	ShaderEntry::Uniform *u = nullptr;
	for (ShaderEntry::Uniform &existing : e->uniforms)
		if (existing.name == uniform) u = &existing;
	if (u != nullptr && u->kind == kind && memcmp(u->values, values, sizeof(u->values)) == 0) return false;
	if (u == nullptr)
	{
		e->uniforms.emplace_back();
		u = &e->uniforms.back();
		u->name = uniform;
	}
	memcpy(u->values, values, sizeof(u->values));
	u->kind = kind;
	table.generation++;
	return true;
}

const ShaderEntry *ComputeShaderEntry(const ShaderTable &table, const std::string &shader, int player) { return FindEntry(table, shader, player); }

void ComputeClearShaderTable(ShaderTable &table)
{
	if (table.entries.empty()) return;
	table.entries.clear();
	table.generation++;
}

bool ComputeShaderCallDeclared(const std::vector<DeclaredShader> &declared, const std::string &shader, const std::string &uniform)
{
	for (const DeclaredShader &d : declared)
	{
		if (!SameName(d.name, shader)) continue;
		if (uniform.empty()) return true;
		for (const std::string &u : d.uniforms)
			if (u == uniform) return true;
	}
	return false;
}

std::vector<DeclarationPart> ComputePostProcessDeclarations(const std::string &lump)
{
	const std::vector<Token> t = Tokens(lump);
	std::vector<DeclarationPart> out;
	int depth = 0;
	for (size_t k = 0; k < t.size(); k++)
	{
		if (!t[k].quoted && t[k].text == "{") { depth++; continue; }
		if (!t[k].quoted && t[k].text == "}") { depth = std::max(0, depth - 1); continue; }
		if (depth > 0 || t[k].quoted || k + 1 >= t.size()) continue;
		if (SameName(t[k].text, "#include") && t[k + 1].quoted) out.push_back({ true, t[++k].text });
		else if (SameName(t[k].text, "hardwareshader") && !t[k + 1].quoted && SameName(t[k + 1].text, "postprocess"))
		{
			const size_t start = t[++k].end;
			size_t end = lump.size();
			// [Doruk-Aksoy] The block runs to the brace closing its first one.
			for (int inner = 0; ++k < t.size();)
			{
				if (t[k].quoted) continue;
				if (t[k].text == "{") inner++;
				else if (t[k].text == "}" && --inner == 0) { end = t[k].end; break; }
			}
			out.push_back({ false, lump.substr(start, end - start) });
		}
	}
	return out;
}

EffectiveEnabled ComputeEffectiveEnabled(const ShaderTable &table, const std::string &shader, int consolePlayer, bool declared, int now)
{
	EffectiveEnabled result = { declared, 0 };
	const ShaderEntry *all = FindEntry(table, shader, -1);
	const ShaderEntry *own = consolePlayer >= 0 ? FindEntry(table, shader, consolePlayer) : nullptr;
	for (const ShaderEntry *e : { all, own })
	{
		// [Doruk-Aksoy] A timed entry that ran out counts as never set, so a player's own effect ending leaves everyone's.
		if (e == nullptr || e->enabled < 0 || Expired(*e, now)) continue;
		result.enabled = e->enabled == 1;
		result.sinceTic = e->enabledTic;
	}
	return result;
}

const double *ComputeEffectiveUniform(const ShaderTable &table, const std::string &shader, int consolePlayer, const std::string &uniform)
{
	if (consolePlayer >= 0)
		if (const double *own = UniformOf(FindEntry(table, shader, consolePlayer), uniform)) return own;
	return UniformOf(FindEntry(table, shader, -1), uniform);
}

std::string ComputeShaderStateLine(const std::string &name, const EffectiveEnabled &state, const std::vector<UniformState> &uniforms)
{
	std::ostringstream s;
	s << name << (state.enabled ? " on since " + std::to_string(state.sinceTic) : std::string(" off"));
	for (const UniformState &u : uniforms)
		s << " " << u.name << "=" << u.values[0] << "," << u.values[1] << "," << u.values[2] << "," << u.values[3];
	return s.str();
}

bool ComputeConsoleEnable(int value, bool current) { return value == -1 ? !current : value != 0; }

std::vector<int> ComputePostProcessChain(const std::vector<int> &stages, const std::vector<bool> &enabled, const std::vector<bool> &ready, int stage)
{
	std::vector<int> chain;
	for (size_t i = 0; i < stages.size(); i++)
		if (stages[i] == stage && i < enabled.size() && enabled[i] && i < ready.size() && ready[i]) chain.push_back((int)i);
	return chain;
}

std::vector<Pass> ComputePingPong(int passes, int first)
{
	std::vector<Pass> out;
	int from = first;
	for (int i = 0; i < passes; i++)
	{
		const int to = from == kPictureA ? kPictureB : kPictureA;
		out.push_back({ from, to });
		from = to;
	}
	return out;
}

int ComputeStageResult(const std::vector<Pass> &passes, int first) { return passes.empty() ? first : passes.back().target; }

Times ComputePostProcessTimes(unsigned frameMs, unsigned previousFrameMs, int maptime, double ticFrac, int sinceTic, int ticRate)
{
	Times t;
	t.inputTime = (float)(frameMs / 1000.0);
	t.inputTimeDelta = (float)((frameMs >= previousFrameMs ? frameMs - previousFrameMs : 0u) / 1000.0);
	const double game = maptime + ticFrac;
	t.inputTimeGame = (float)(game / ticRate);
	t.enabledTime = (float)(std::max(0.0, game - sinceTic) / ticRate);
	return t;
}

void ComputeAutoBlock(const Times &times, const float region[4], const float lastRegion[4], int outW, int outH, float out[kAutoBlockFloats])
{
	out[0] = times.inputTimeDelta;
	out[1] = times.inputTime;
	out[2] = times.inputTimeGame;
	out[3] = times.enabledTime;
	memcpy(out + 4, region, sizeof(float) * 4);
	memcpy(out + 8, lastRegion, sizeof(float) * 4);
	out[12] = (float)std::max(outW, 1);
	out[13] = (float)std::max(outH, 1);
	out[14] = out[15] = 0.f;
}

void ComputePostRegion(int w, int h, int targetW, int targetH, float out[4])
{
	out[0] = targetW > 0 ? (float)w / (float)targetW : 1.f;
	out[1] = targetH > 0 ? (float)h / (float)targetH : 1.f;
	out[2] = (float)std::max(w, 1);
	out[3] = (float)std::max(h, 1);
}

}} // namespace zx::postprocess

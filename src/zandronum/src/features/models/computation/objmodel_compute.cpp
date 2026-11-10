// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/models/computation/objmodel_compute.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace zx { namespace models {

namespace {

// [Doruk-Aksoy] A face as read, its corners resolved against the counts at its line, checked once the whole file is read since a corner may name a position defined after it.
struct RawFace
{
	unsigned firstCorner, count;
	bool unreadable;
};

struct RawSurface
{
	std::string material;
	std::vector<unsigned> faces;
};

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

// [Doruk-Aksoy] The line's words, up to a '#' comment.
void Words(const char *begin, const char *end, std::vector<std::string> &out)
{
	out.clear();
	const char *p = begin;
	while (p < end && *p != '#')
	{
		while (p < end && IsSpace(*p)) p++;
		const char *w = p;
		while (p < end && !IsSpace(*p) && *p != '#') p++;
		if (p > w) out.push_back(std::string(w, p));
	}
}

bool ReadFloat(const std::string &s, float &out)
{
	char *end = NULL;
	out = std::strtof(s.c_str(), &end);
	return end != s.c_str() && *end == '\0' && std::isfinite(out);
}

bool ReadIndex(const std::string &s, long long &out)
{
	char *end = NULL;
	out = std::strtoll(s.c_str(), &end, 10);
	return end != s.c_str() && *end == '\0';
}

// [Doruk-Aksoy] OBJ counts from 1, and a negative index back from the last one written so far; 0 names nothing.
long long Resolve(long long index, size_t written)
{
	if (index > 0) return index - 1;
	if (index < 0) return (long long)written + index;
	return -1;
}

// [Doruk-Aksoy] Reads `count` numbers after the keyword into `to`, 0 for any missing or unreadable, and whether all were there and read.
bool ReadNumbers(const std::vector<std::string> &words, int count, int required, std::vector<float> &to)
{
	bool ok = (int)words.size() - 1 >= required;
	for (int i = 0; i < count; i++)
	{
		float f = 0.f;
		if (i + 1 < (int)words.size() && !ReadFloat(words[i + 1], f)) { ok = false; f = 0.f; }
		to.push_back(f);
	}
	return ok;
}

// [Doruk-Aksoy] One `f` corner, `v`, `v/vt`, `v//vn` or `v/vt/vn`, resolved against the positions and coordinates so far; false when it cannot be read.
bool ReadCorner(const std::string &word, size_t positions, size_t uvs, long long &vertex, long long &uv)
{
	std::vector<std::string> parts(1);
	for (size_t i = 0; i < word.size(); i++)
	{
		if (word[i] == '/') parts.push_back(std::string());
		else parts.back() += word[i];
	}
	long long v;
	if (parts.size() > 3 || !ReadIndex(parts[0], v)) return false;
	vertex = Resolve(v, positions);
	uv = -1;
	if (parts.size() > 1 && !parts[1].empty())
	{
		long long t;
		if (!ReadIndex(parts[1], t)) return false;
		uv = Resolve(t, uvs);
	}
	return true;
}

// [Doruk-Aksoy] The corners of triangle `t` of a face of `n`, as UZDoom splits it: a quad 0-1-3 and 1-2-3, anything else a fan from corner 0.
void TriangleCorners(unsigned n, unsigned t, unsigned out[3])
{
	static const unsigned quad[2][3] = { { 0, 1, 3 }, { 1, 2, 3 } };
	if (n == 4)
	{
		out[0] = quad[t][0]; out[1] = quad[t][1]; out[2] = quad[t][2];
		return;
	}
	out[0] = 0; out[1] = t + 1; out[2] = t + 2;
}

} // namespace

bool ComputeObjParse(const char *text, size_t length, ObjData &out, ObjProblems &problems)
{
	out = ObjData();
	problems.badLines = problems.droppedFaces = problems.badUVs = 0;
	std::vector<long long> rawVertex, rawUV;
	std::vector<RawFace> faces;
	std::vector<RawSurface> surfaces(1);
	std::vector<std::string> words;
	const char *p = text, *end = text + length;
	while (p < end)
	{
		const char *line = p;
		while (p < end && *p != '\n') p++;
		Words(line, p, words);
		if (p < end) p++;
		if (words.empty()) continue;
		const std::string &key = words[0];
		if (key == "v")
		{
			if (!ReadNumbers(words, 3, 3, out.positions)) problems.badLines++;
		}
		else if (key == "vt")
		{
			if (!ReadNumbers(words, 2, 1, out.uvs)) problems.badLines++;
		}
		else if (key == "f")
		{
			RawFace f = { (unsigned)rawVertex.size(), (unsigned)words.size() - 1, false };
			for (size_t i = 1; i < words.size(); i++)
			{
				long long v = -1, t = -1;
				if (!ReadCorner(words[i], out.positions.size() / 3, out.uvs.size() / 2, v, t)) f.unreadable = true;
				rawVertex.push_back(v);
				rawUV.push_back(t);
			}
			surfaces.back().faces.push_back((unsigned)faces.size());
			faces.push_back(f);
		}
		else if (key == "usemtl")
		{
			// [Doruk-Aksoy] The name is the rest of the line, which may hold spaces; a run with no faces yet just takes the new name.
			std::string name;
			for (size_t i = 1; i < words.size(); i++) name += (i > 1 ? " " : "") + words[i];
			if (name.empty()) problems.badLines++;
			if (!surfaces.back().faces.empty()) surfaces.push_back(RawSurface());
			surfaces.back().material = name;
		}
	}

	// [Doruk-Aksoy] Every face checked against the whole file: a corner past the positions, or under three corners, drops the face rather than reading past them.
	const long long positions = (long long)(out.positions.size() / 3), uvs = (long long)(out.uvs.size() / 2);
	for (size_t s = 0; s < surfaces.size(); s++)
	{
		ObjSurface kept = { surfaces[s].material, (unsigned)out.faceStart.size(), 0 };
		for (size_t k = 0; k < surfaces[s].faces.size(); k++)
		{
			const RawFace &f = faces[surfaces[s].faces[k]];
			bool usable = !f.unreadable && f.count >= 3;
			for (unsigned c = 0; usable && c < f.count; c++)
				usable = rawVertex[f.firstCorner + c] >= 0 && rawVertex[f.firstCorner + c] < positions;
			if (!usable) { problems.droppedFaces++; continue; }
			out.faceStart.push_back((unsigned)out.corners.size());
			for (unsigned c = 0; c < f.count; c++)
			{
				long long t = rawUV[f.firstCorner + c];
				if (t != -1 && (t < 0 || t >= uvs)) { problems.badUVs++; t = -1; }
				const ObjCorner corner = { (int)rawVertex[f.firstCorner + c], (int)t };
				out.corners.push_back(corner);
			}
			kept.faceCount++;
		}
		if (kept.faceCount > 0) out.surfaces.push_back(kept);
	}
	return !out.faceStart.empty();
}

void ComputeObjTriangles(const ObjData &data, std::vector<ObjDrawVertex> &out, std::vector<unsigned> &surfaceFirst, std::vector<unsigned> &surfaceCount)
{
	out.clear();
	surfaceFirst.clear();
	surfaceCount.clear();
	const size_t faces = data.faceStart.size();
	for (size_t s = 0; s < data.surfaces.size(); s++)
	{
		const ObjSurface &surface = data.surfaces[s];
		surfaceFirst.push_back((unsigned)out.size());
		for (unsigned f = surface.firstFace; f < surface.firstFace + surface.faceCount; f++)
		{
			const unsigned first = data.faceStart[f];
			const unsigned n = (f + 1 < faces ? data.faceStart[f + 1] : (unsigned)data.corners.size()) - first;
			for (unsigned t = 0; t + 2 < n; t++)
			{
				unsigned tri[3];
				TriangleCorners(n, t, tri);
				// [Doruk-Aksoy] UZDoom writes each triangle's corners last to first.
				for (int side = 2; side >= 0; side--)
				{
					const ObjCorner &c = data.corners[first + tri[side]];
					const float *pos = &data.positions[(size_t)c.vertex * 3];
					const size_t uv = c.uv >= 0 ? (size_t)c.uv : 0;
					const float u = data.uvs.empty() ? 0.f : data.uvs[uv * 2];
					const float v = data.uvs.empty() ? 0.f : data.uvs[uv * 2 + 1];
					const ObjDrawVertex d = { pos[0], pos[1], -pos[2], u, 1.f - v };
					out.push_back(d);
				}
			}
		}
		surfaceCount.push_back((unsigned)out.size() - surfaceFirst.back());
	}
}

}} // namespace zx::models

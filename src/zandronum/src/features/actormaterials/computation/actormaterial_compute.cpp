// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actormaterials/computation/actormaterial_compute.h"

#include <cstddef>
#include <cstring>
#include <sstream>

namespace zx { namespace actormaterials {

bool ComputeIsActorMaterialFunction(int function)
{
	return function >= kSetActorMaterial && function <= kSetActorUniform1i;
}

ActorUniformCall ComputeActorUniformCall(int function, const int *args, int argCount)
{
	ActorUniformCall c = {};
	const int floats = function == kSetActorUniform1i ? 0 : function - kSetActorUniform1f + 1;
	if (function < kSetActorUniform1f || function > kSetActorUniform1i || argCount < 2 + (floats > 0 ? floats : 1)) return c;
	c.ok = true;
	c.tid = args[0];
	c.uniform = args[1];
	c.value.kind = floats;
	if (floats == 0) c.value.values[0] = args[2];
	for (int i = 0; i < floats; i++) c.value.values[i] = args[2 + i] / 65536.0;
	return c;
}

int ComputeAcquireInstance(InstanceTable &table, int material)
{
	size_t slot = 0;
	while (slot < table.instances.size() && table.instances[slot].used) slot++;
	if (slot >= (size_t)kMaxActorInstances) return -1;
	if (slot == table.instances.size()) table.instances.push_back(ActorInstance());
	ActorInstance &a = table.instances[slot];
	a = ActorInstance();
	a.used = true;
	a.material = material;
	table.generation++;
	return (int)slot;
}

void ComputeReleaseInstance(InstanceTable &table, int slot)
{
	if (slot < 0 || slot >= (int)table.instances.size() || !table.instances[slot].used) return;
	table.instances[slot] = ActorInstance();
	table.generation++;
}

bool ComputeSetInstanceUniform(InstanceTable &table, int slot, int uniform, const zx::acsshaders::UniformValue &value)
{
	if (slot < 0 || slot >= (int)table.instances.size() || !table.instances[slot].used || uniform < 0 || uniform >= kActorUniformVec4s) return false;
	ActorInstance &a = table.instances[slot];
	double d[4];
	zx::acsshaders::ComputeUniformValues(value, d);
	float v[4] = { (float)d[0], (float)d[1], (float)d[2], (float)d[3] };
	const unsigned bit = 1u << uniform;
	if ((a.set & bit) != 0 && memcmp(a.values[uniform], v, sizeof(v)) == 0) return false;
	a.set |= bit;
	memcpy(a.values[uniform], v, sizeof(v));
	table.generation++;
	return true;
}

void ComputeClearInstances(InstanceTable &table)
{
	if (table.instances.empty()) return;
	table.instances.clear();
	table.generation++;
}

int ComputeActorUniformRow(int material, int slot, int materialCount)
{
	return slot >= 0 ? materialCount + slot : material;
}

void ComputeActorUniformRows(const std::vector<MaterialDefaults> &defaults, const InstanceTable &table, std::vector<float> &out)
{
	const size_t rows = defaults.size() + table.instances.size();
	out.assign(rows * kActorUniformVec4s * 4, 0.f);
	const auto fill = [&](size_t row, int material, const ActorInstance *own) {
		for (int u = 0; u < kActorUniformVec4s; u++)
		{
			float *to = &out[(row * kActorUniformVec4s + u) * 4];
			if (own != nullptr && (own->set & (1u << u)) != 0) memcpy(to, own->values[u], sizeof(float) * 4);
			else if (material >= 0 && material < (int)defaults.size() && u < (int)defaults[material].size()) memcpy(to, defaults[material][u].data(), sizeof(float) * 4);
		}
	};
	for (size_t m = 0; m < defaults.size(); m++) fill(m, (int)m, nullptr);
	for (size_t s = 0; s < table.instances.size(); s++)
		if (table.instances[s].used) fill(defaults.size() + s, table.instances[s].material, &table.instances[s]);
}

std::string ComputeActorUniformLine(int tid, const std::string &material, const std::vector<std::string> &uniforms, const MaterialDefaults &defaults, const ActorInstance *own)
{
	std::ostringstream s;
	s << "tid=" << tid << " " << material;
	for (size_t u = 0; u < uniforms.size() && u < (size_t)kActorUniformVec4s; u++)
	{
		const bool set = own != nullptr && (own->set & (1u << u)) != 0;
		const float *v = set ? own->values[u] : u < defaults.size() ? defaults[u].data() : nullptr;
		s << " " << uniforms[u] << "=";
		for (int k = 0; k < 4; k++) s << (k ? "," : "") << (v != nullptr ? v[k] : 0.f);
	}
	return s.str();
}

}} // namespace zx::actormaterials

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dev-mode/devmode.h"
#include "features/dev-mode/computation/devmode_compute.h"
#include "features/net-session/netrole.h"
#include "c_cvars.h"
#include "cl_commands.h"
#include "doomtype.h"

#include <map>
#include <string>

EXTERN_CVAR(Bool, sv_cheats)

namespace zx { namespace devmode {

namespace {

bool g_open = true;
// [rc4l] Each held setting's own value, given back when the gate opens.
std::map<const FBaseCVar *, std::string> g_held;

void Refuse(const char *name)
{
	// [rc4l] CheckCheatmode's throttle, so a bound key cannot flood a client with the message.
	if (!zx::net::IsClient() || CLIENT_AllowSVCheatMessage())
		Printf("%s is a dev-mode tool: sv_cheats must be true on this server.\n", name);
}

} // namespace

void Refresh()
{
	const bool open = ComputeDevModeAllowed(zx::net::IsClient(), sv_cheats);
	const DevModeChange change = ComputeDevModeChange(g_open, open);
	if (change == kDevModeStay) return;
	g_open = open;
	for (FBaseCVar *var = CVars; var != NULL; var = var->GetNext())
	{
		if (!(var->GetFlags() & CVAR_FUA_DEV_MODE)) continue;
		if (change == kDevModeHold)
		{
			const std::string own = var->GetGenericRep(CVAR_String).String;
			if (!ComputeDevModeHolds(own.c_str(), var->GetGenericRepDefault(CVAR_String).String)) continue;
			g_held[var] = own;
			ECVarType type;
			const UCVarValue def = var->GetFavoriteRepDefault(&type);
			// [rc4l] ForceSet, which passes by SetGenericRep's check on this gate.
			var->ForceSet(def, type);
		}
		else
		{
			std::map<const FBaseCVar *, std::string>::const_iterator it = g_held.find(var);
			if (it == g_held.end()) continue;
			UCVarValue own;
			own.String = it->second.c_str();
			var->ForceSet(own, CVAR_String);
		}
	}
	if (change == kDevModeRelease) g_held.clear();
}

bool MaySet(const FBaseCVar &var)
{
	Refresh();
	if (g_open) return true;
	Refuse(var.GetName());
	return false;
}

bool MayRun(const char *name)
{
	Refresh();
	if (g_open) return true;
	Refuse(name);
	return false;
}

const char *HeldValue(const FBaseCVar &var)
{
	std::map<const FBaseCVar *, std::string>::const_iterator it = g_held.find(&var);
	return it == g_held.end() ? NULL : it->second.c_str();
}

}} // namespace zx::devmode

//-----------------------------------------------------------------------------
//
// [ForkUnderA] cl_fua_caching -- level-load asset precache (see fua_caching.h)
//
// The spawn closure works off a fact of this DECORATE dialect: a literal
// class argument like A_SpawnItemEx("FooDebris") is constant-folded into an
// FxConstant holding the resolved PClass at parse time (FxClassTypeCast::
// Resolve), and sound literals fold the same way. So the closure never runs
// the expression evaluator against a live actor -- it only reads constants
// out of the global StateParams table, keyed by the class that owns each
// state. Arguments computed at run time can't be resolved here and simply
// stay lazy-loaded, which is the stock behavior.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "doomstat.h"
#include "templates.h"
#include "actor.h"
#include "info.h"
#include "dthinker.h"
#include "g_level.h"
#include "r_state.h"
#include "s_sound.h"
#include "c_cvars.h"
#include "c_console.h"
#include "thingdef/thingdef.h"
#include "thingdef/thingdef_exp.h"
#include "features/fua-caching/fua_caching.h"

CVAR (Int, cl_fua_caching, 2, CVAR_ARCHIVE)

// Backstop against pathological mods whose spawn graph reaches everything;
// what is cut off just stays lazy-loaded.
static const unsigned int FUA_CACHING_MAX_CLASSES = 4096;

struct FStateRefs;
static TMap<const PClass *, FStateRefs> g_stateRefs;
static bool g_stateRefsBuilt;

void FUA_CachingReset ()
{
	g_stateRefs.Clear();
	g_stateRefsBuilt = false;
}

int FUA_CachingMode ()
{
	return clamp<int> (cl_fua_caching, 0, 2);
}

//==========================================================================
//
// Constant class/sound references in DECORATE state parameters, grouped by
// the class that owns the states. StateParams is immutable once DECORATE
// has been parsed, so this is built once per process on first use.
//
//==========================================================================

struct FStateRefs
{
	TArray<const PClass *> classes;
	TArray<int> sounds;
};


static void BuildStateRefs ()
{
	for (unsigned int i = 0; i < StateParams.Size (); ++i)
	{
		FxExpression *x = StateParams.Get (i);
		if (x == NULL || !x->isConstant ())
			continue;
		const PClass *owner = StateParams.GetOwner (i);
		if (owner == NULL)
			continue;

		ExpVal val = x->EvalExpression (NULL);
		if (val.Type == VAL_Class)
		{
			const PClass *cls = val.GetClass ();
			if (cls != NULL)
				g_stateRefs[owner].classes.Push (cls);
		}
		else if (val.Type == VAL_Sound)
		{
			int id = val.GetSoundID ();
			if (id > 0)
				g_stateRefs[owner].sounds.Push (id);
		}
	}
	g_stateRefsBuilt = true;
}

//==========================================================================
//
// The cached-class set for the current level: classes of all spawned
// actors, MAPINFO PrecacheClasses, and (mode 2) the closure over constant
// spawn references, drop items, replacements and blood types.
//
//==========================================================================

static void AddClass (const PClass *cls, TArray<const PClass *> &open, TMap<const PClass *, BYTE> &seen)
{
	if (cls == NULL || cls->ActorInfo == NULL)
		return;
	if (seen.CheckKey (cls) != NULL)
		return;
	seen[cls] = 1;
	open.Push (cls);
}

static void CollectCachedClasses (TArray<const PClass *> &out)
{
	out.Clear ();
	const int mode = FUA_CachingMode ();
	if (mode < 1)
		return;
	if (mode >= 2 && !g_stateRefsBuilt)
		BuildStateRefs ();

	TArray<const PClass *> open;
	TMap<const PClass *, BYTE> seen;

	{
		AActor *actor;
		TThinkerIterator<AActor> iterator;
		while ((actor = iterator.Next ()) != NULL)
			AddClass (actor->GetClass (), open, seen);
	}
	if (level.info != NULL)
	{
		for (unsigned int c = 0; c < level.info->PrecacheClasses.Size (); ++c)
			AddClass (PClass::FindClass (level.info->PrecacheClasses[c]), open, seen);
	}

	while (open.Size () > 0 && out.Size () < FUA_CACHING_MAX_CLASSES)
	{
		const PClass *cls = open[open.Size () - 1];
		open.Delete (open.Size () - 1);
		out.Push (cls);

		if (mode < 2)
			continue;

		// Spawn calls go through ALLOW_REPLACE, so a reachable class means
		// its replacement is what actually appears.
		AddClass (cls->GetReplacement (), open, seen);

		// Constant class/sound refs live with the class that owns the state,
		// so inherited states mean walking the ancestor chain.
		for (const PClass *anc = cls; anc != NULL; anc = anc->ParentClass)
		{
			FStateRefs *refs = g_stateRefs.CheckKey (anc);
			if (refs != NULL)
			{
				for (unsigned int j = 0; j < refs->classes.Size (); ++j)
					AddClass (refs->classes[j], open, seen);
			}
		}

		int index = cls->Meta.GetMetaInt (ACMETA_DropItems) - 1;
		if (index >= 0 && index < (int)DropItemList.Size ())
		{
			for (FDropItem *di = DropItemList[index]; di != NULL; di = di->Next)
			{
				const PClass *drop = PClass::FindClass (di->Name);
				AddClass (drop, open, seen);
			}
		}

		AddClass (PClass::FindClass ((ENamedName)cls->Meta.GetMetaInt (AMETA_BloodType, NAME_Blood)), open, seen);
		AddClass (PClass::FindClass ((ENamedName)cls->Meta.GetMetaInt (AMETA_BloodType2, NAME_BloodSplatter)), open, seen);
		AddClass (PClass::FindClass ((ENamedName)cls->Meta.GetMetaInt (AMETA_BloodType3, NAME_AxeBlood)), open, seen);
	}

	if (open.Size () > 0)
		Printf (TEXTCOLOR_YELLOW "FUA caching: class cap (%u) reached; %u classes stay lazy-loaded\n",
			FUA_CACHING_MAX_CLASSES, open.Size ());
}

//==========================================================================
//
// FUA_MarkCachedSprites
//
//==========================================================================

void FUA_MarkCachedSprites (BYTE *spritelist, unsigned int numsprites)
{
	TArray<const PClass *> classes;
	CollectCachedClasses (classes);

	// An actor displays states owned by its class and every ancestor; shared
	// ancestor chains are walked once.
	TMap<const PClass *, BYTE> done;
	for (unsigned int i = 0; i < classes.Size (); ++i)
	{
		for (const PClass *anc = classes[i]; anc != NULL && anc->ActorInfo != NULL; anc = anc->ParentClass)
		{
			if (done.CheckKey (anc) != NULL)
				break;
			done[anc] = 1;

			const FActorInfo *ai = anc->ActorInfo;
			for (int s = 0; s < ai->NumOwnedStates; ++s)
			{
				WORD spr = ai->OwnedStates[s].sprite;
				if (spr < numsprites)
					spritelist[spr] = 1;
			}
		}
	}
}

//==========================================================================
//
// FUA_MarkCachedSounds
//
//==========================================================================

void FUA_MarkCachedSounds ()
{
	TArray<const PClass *> classes;
	CollectCachedClasses (classes);
	const int mode = FUA_CachingMode ();

	TMap<const PClass *, BYTE> done;
	for (unsigned int i = 0; i < classes.Size (); ++i)
	{
		// Non-virtual on purpose: Defaults are memcpy-built, so their vtable
		// pointer must not be trusted. The subclass sound additions (weapon
		// up/ready, player sound class, ambients) are covered by the
		// live-actor pass in S_PrecacheLevel.
		const PClass *cls = classes[i];
		if (cls->Defaults != NULL)
			((const AActor *)cls->Defaults)->MarkPropertySounds ();

		if (mode < 2)
			continue;
		for (const PClass *anc = cls; anc != NULL; anc = anc->ParentClass)
		{
			if (done.CheckKey (anc) != NULL)
				break;
			done[anc] = 1;

			FStateRefs *refs = g_stateRefs.CheckKey (anc);
			if (refs != NULL)
			{
				for (unsigned int j = 0; j < refs->sounds.Size (); ++j)
				{
					if (refs->sounds[j] > 0 && refs->sounds[j] < (int)S_sfx.Size ())
						FSoundID (refs->sounds[j]).MarkUsed ();
				}
			}
		}
	}
}

/*
** gldefs.cpp
** Light definitions for actors.
**
**---------------------------------------------------------------------------
** Copyright 2003 Timothy Stump
** Copyright 2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


#include <ctype.h>
#include <cstring>
#include "i_system.h"
#include "doomtype.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "m_random.h"
#include "sc_man.h"
#include "templates.h"
#include "w_wad.h"
#include "gi.h"
#include "r_state.h"
#include "stats.h"
#include "zstring.h"
#include "d_dehacked.h"


// [rc4l] Moved from gl/dynlights/gl_dynlight.cpp, as both renderers draw what GLDEFS defines.
#include "xs_Float.h"
#include "features/dynlights/dynlight.h"
#include "features/gldefs/gldefs.h"
#include "features/gldefs/computation/lighttags_compute.h"
#include "features/gldefs/computation/lightsize_compute.h"
#include "features/gldefs/glow.h"
#include "features/hwshaders/hwshaders.h"
#include "features/postprocess/postprocess.h"
#include "features/actormaterials/actormaterials.h"   // [Doruk-Aksoy] ClearDeclarations, as GLDEFS is read again
#include "features/hwrender/backendonly.h"   // [Doruk-Aksoy] BackendOnly, for the post-process note
#include "features/materials/skyboxtexture.h"
// [BB] New #includes.
#include "network.h"
#include "features/net-session/netrole.h"

using zx::gldefs::LightBlock;
using zx::gldefs::LightTag;
using zx::gldefs::ComputeLightTag;
using zx::gldefs::ComputeLightTagAccepted;

EXTERN_CVAR (Float, r_lights_intensity);
EXTERN_CVAR (Float, r_lights_size);
int ScriptDepth;
void gl_ParseBrightmap(FScanner &sc, int);
void gl_ParseSkybox(FScanner &sc);
void gl_ParseDetailTexture(FScanner &sc);
void gl_ParseVavoomSkybox();

//==========================================================================
//
// Dehacked aliasing
//
//==========================================================================

inline const PClass * GetRealType(const PClass * ti)
{
	FActorInfo *rep = ti->ActorInfo->GetReplacement(false);
	if (rep != ti->ActorInfo && rep != NULL && rep->Class->IsDescendantOf(RUNTIME_CLASS(ADehackedPickup)))
	{
		return rep->Class;
	}
	return ti;
}



//==========================================================================
//
// Light associations
//
//==========================================================================
class FLightAssociation
{
public:
	//FLightAssociation();
	FLightAssociation(FName actorName, const char *frameName, FName lightName)
		: m_ActorName(actorName), m_AssocLight(lightName)
	{
		// [rc4l] uzdoom@045725de1: m_FrameName is a fixed 8-byte array, not a C string buffer;
		// mysnprintf would reserve a byte for a terminator this field does not carry.
		strncpy(m_FrameName, frameName, 8);
	}

	FName ActorName() { return m_ActorName; }
	const char *FrameName() { return m_FrameName; }
	FName Light() { return m_AssocLight; }
	void ReplaceLightName(FName newName) { m_AssocLight = newName; }
protected:
	char m_FrameName[8];
	FName m_ActorName, m_AssocLight;
};

TArray<FLightAssociation> LightAssociations;


//==========================================================================
//
// Light definitions
//
//==========================================================================
class FLightDefaults
{
public:
   FLightDefaults(FName name, ELightType type);

   void ApplyProperties(ADynamicLight * light) const;
   FName GetName() const { return m_Name; }
   void SetAngle(angle_t angle) { m_Angle = angle; }
   void SetArg(int arg, int val) { m_Args[arg] = val; }
   int GetArg(int arg) { return m_Args[arg]; }
   void SetOffset(float* ft) { m_X = FLOAT2FIXED(ft[0]); m_Y = FLOAT2FIXED(ft[1]); m_Z = FLOAT2FIXED(ft[2]); }
   void SetSubtractive(bool subtract) { m_subtractive = subtract; }
   void SetAdditive(bool add) { m_additive = add; }
   void SetDontLightSelf(bool add) { m_dontlightself = add; }
   void SetHalo(bool halo) { m_halo = halo; }
   void SetLightDefIntensity(float intensity) { m_lightDefIntensity = intensity; }
   void SetDontLightActors(bool on) { m_dontLightActors = on; }
   void SetDontLightOthers(bool on) { m_dontLightOthers = on; }
protected:
   FName m_Name;
   int m_Args[5];   // [Doruk-Aksoy] ints, as UZDoom's, so a size reaches 1024
   angle_t m_Angle;
   fixed_t m_X, m_Y, m_Z;
   ELightType m_type;
   bool m_subtractive, m_additive, m_halo, m_dontlightself;
   float m_lightDefIntensity;
   bool m_dontLightActors, m_dontLightOthers;
};

TArray<FLightDefaults *> LightDefaults;

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

FLightDefaults::FLightDefaults(FName name, ELightType type)
{
	m_Name = name;
	m_type = type;

	m_X = m_Y = m_Z = 0;
	memset(m_Args, 0, sizeof(m_Args));

	m_subtractive = false;
	m_additive = false;
	m_halo = false;
	m_dontlightself = false;
	m_lightDefIntensity = 1.f;
	m_dontLightActors = false;
	m_dontLightOthers = false;
}

void FLightDefaults::ApplyProperties(ADynamicLight * light) const
{
	light->lighttype = m_type;
	light->angle = m_Angle;
	light->SetOffset(m_X, m_Y, m_Z);
	light->halo = m_halo;
	light->lightDefIntensity = m_lightDefIntensity;
	light->dontLightActors = m_dontLightActors;
	light->dontLightOthers = m_dontLightOthers;
	for (int a = 0; a < 3; a++) light->args[a] = clamp<int>((int)(m_Args[a] * r_lights_intensity), 0, 255);
	light->m_intensity[0] = int(m_Args[LIGHT_INTENSITY]);
	light->m_intensity[1] = int(m_Args[LIGHT_SECONDARY_INTENSITY]);
	light->flags4&=~(MF4_ADDITIVE|MF4_SUBTRACTIVE|MF4_DONTLIGHTSELF);
	if (m_subtractive) light->flags4|=MF4_SUBTRACTIVE;
	if (m_additive) light->flags4|=MF4_ADDITIVE;
	if (m_dontlightself) light->flags4|=MF4_DONTLIGHTSELF;
}


//==========================================================================
//
// light definition file parser
//
//==========================================================================



extern int ScriptDepth;


//==========================================================================
//
//
//
//==========================================================================

inline float gl_ParseFloat(FScanner &sc)
{
   sc.GetFloat();

   return float(sc.Float);
}


inline int gl_ParseInt(FScanner &sc)
{
   sc.GetNumber();

   return sc.Number;
}


inline char *gl_ParseString(FScanner &sc)
{
   sc.GetString();

   return sc.String;
}


void gl_ParseTriple(FScanner &sc, float floatVal[3])
{
   for (int i = 0; i < 3; i++)
   {
      floatVal[i] = gl_ParseFloat(sc);
   }
}


void gl_AddLightDefaults(FLightDefaults *defaults)
{
   FLightDefaults *temp;
   unsigned int i;

   // remove duplicates
   for (i = 0; i < LightDefaults.Size(); i++)
   {
      temp = LightDefaults[i];
	  if (temp->GetName() == defaults->GetName())
      {
         delete temp;
         LightDefaults.Delete(i);
         break;
      }
   }

   LightDefaults.Push(defaults);
}


//-----------------------------------------------------------------------------
//
// [Doruk-Aksoy] One parser for the five light blocks, each taking the tags ComputeLightTagAccepted gives it.
//
//-----------------------------------------------------------------------------

static void gl_ParseLight(FScanner &sc, LightBlock block, ELightType lightType)
{
	float floatVal;
	float floatTriple[3];

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (!sc.Compare("{"))
	{
		sc.ScriptError("Expected '{'.\n");
		return;
	}

	FLightDefaults *defaults = new FLightDefaults(name, lightType);
	ScriptDepth++;
	while (ScriptDepth)
	{
		sc.GetString();
		const LightTag tag = ComputeLightTag(sc.String);
		if (!ComputeLightTagAccepted(block, tag))
		{
			sc.ScriptError("Unknown tag: %s\n", sc.String);
		}
		switch (tag)
		{
		case LightTag::OpenBrace:
			ScriptDepth++;
			break;
		case LightTag::CloseBrace:
			ScriptDepth--;
			break;
		case LightTag::Color:
			gl_ParseTriple(sc, floatTriple);
			defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
			defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
			defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
			break;
		case LightTag::Offset:
			gl_ParseTriple(sc, floatTriple);
			defaults->SetOffset(floatTriple);
			break;
		case LightTag::Size:
			defaults->SetArg(LIGHT_INTENSITY, zx::gldefs::ComputeLightSize(gl_ParseInt(sc)));
			break;
		case LightTag::SecondarySize:
			defaults->SetArg(LIGHT_SECONDARY_INTENSITY, zx::gldefs::ComputeLightSize(gl_ParseInt(sc)));
			break;
		case LightTag::Interval:
			floatVal = gl_ParseFloat(sc);
			// [Doruk-Aksoy] A pulse light's interval is seconds; a random flicker light's is a fraction of a turn, as its chance is.
			if (block == LightBlock::Pulse) defaults->SetAngle(xs_RoundToUInt(floatVal * TICRATE / 180. * ANGLE_180));
			else defaults->SetAngle((angle_t)(floatVal * ANGLE_MAX));
			break;
		case LightTag::Chance:
			floatVal = gl_ParseFloat(sc);
			defaults->SetAngle((angle_t)(floatVal * ANGLE_MAX));
			break;
		case LightTag::Scale:
			floatVal = gl_ParseFloat(sc);
			defaults->SetArg(LIGHT_SCALE, zx::gldefs::ComputeSectorLightScale(floatVal));
			break;
		case LightTag::Subtractive:
			defaults->SetSubtractive(gl_ParseInt(sc) != 0);
			break;
		case LightTag::Additive:
			defaults->SetAdditive(gl_ParseInt(sc) != 0);
			break;
		case LightTag::Halo:
			defaults->SetHalo(gl_ParseInt(sc) != 0);
			break;
		case LightTag::DontLightSelf:
			defaults->SetDontLightSelf(gl_ParseInt(sc) != 0);
			break;
		case LightTag::Attenuate:
			// [rc4l] GZDoom's attenuation flag, read and dropped (legacy GL always attenuates), so wads that set it, e.g. Judgment, still load.
			gl_ParseInt(sc);
			break;
		case LightTag::DontLightActors:
			defaults->SetDontLightActors(gl_ParseInt(sc) != 0);
			break;
		case LightTag::DontLightOthers:
			defaults->SetDontLightOthers(gl_ParseInt(sc) != 0);
			break;
		case LightTag::NoShadowmap:
		case LightTag::DontLightMap:
			// [Doruk-Aksoy] UZDoom's shadow map and lightmap flags, read and dropped since we have neither, so a UZDoom GLDEFS loads.
			gl_ParseInt(sc);
			break;
		case LightTag::Spot:
			// [Doruk-Aksoy] UZDoom's inner and outer cone angles, read and not yet acted on.
			gl_ParseFloat(sc);
			gl_ParseFloat(sc);
			break;
		case LightTag::Intensity:
			defaults->SetLightDefIntensity(gl_ParseFloat(sc));
			break;
		default:
			break;
		}
	}
	if (block == LightBlock::Flicker2 && defaults->GetArg(LIGHT_SECONDARY_INTENSITY) < defaults->GetArg(LIGHT_INTENSITY))
	{
		int v = defaults->GetArg(LIGHT_SECONDARY_INTENSITY);
		defaults->SetArg(LIGHT_SECONDARY_INTENSITY, defaults->GetArg(LIGHT_INTENSITY));
		defaults->SetArg(LIGHT_INTENSITY, v);
	}
	gl_AddLightDefaults(defaults);
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void gl_AddLightAssociation(const char *actor, const char *frame, const char *light)
{
	FLightAssociation *temp;
	unsigned int i;
	FLightAssociation assoc(actor, frame, light);

	for (i = 0; i < LightAssociations.Size(); i++)
	{
		temp = &LightAssociations[i];
		if (temp->ActorName() == assoc.ActorName())
		{
			if (strcmp(temp->FrameName(), assoc.FrameName()) == 0)
			{
				temp->ReplaceLightName(assoc.Light());
				return;
			}
		}
	}

	LightAssociations.Push(assoc);
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void gl_ParseFrame(FScanner &sc, FString name)
{
	int startDepth;
	FString frameName;

	// get name
	sc.GetString();
	if (strlen(sc.String) > 8)
	{
		sc.ScriptError("Name longer than 8 characters: %s\n", sc.String);
	}
	frameName = sc.String;

	startDepth = ScriptDepth;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		ScriptDepth++;
		while (ScriptDepth > startDepth)
		{
			sc.GetString();
			const LightTag tag = ComputeLightTag(sc.String);
			if (!ComputeLightTagAccepted(LightBlock::Frame, tag))
			{
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
			switch (tag)
			{
			case LightTag::OpenBrace:
				ScriptDepth++;
				break;
			case LightTag::CloseBrace:
				ScriptDepth--;
				break;
			case LightTag::Light:
				gl_ParseString(sc);
				// [BB] The server just ignores all light associations.
				if ( !zx::net::IsServer() )
					gl_AddLightAssociation(name, frameName, sc.String);
				break;
			case LightTag::Attenuate:
				// [rc4l] GZDoom attenuation flag: accept + consume its arg (legacy GL always
				// attenuates), so MBF21/GZDoom wads that set it -- e.g. Judgment -- still load.
				gl_ParseInt(sc);
				break;
			default:
				break;
			}
		}
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void gl_ParseObject(FScanner &sc)
{
	FString name;

	// get name
	sc.GetString();
	name = sc.String;
	if (!PClass::FindClass(name))
		sc.ScriptMessage("Warning: dynamic lights attached to non-existent actor %s\n", name.GetChars());

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			const LightTag tag = ComputeLightTag(sc.String);
			if (!ComputeLightTagAccepted(LightBlock::Object, tag))
			{
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
			switch (tag)
			{
			case LightTag::OpenBrace:
				ScriptDepth++;
				break;
			case LightTag::CloseBrace:
				ScriptDepth--;
				break;
			case LightTag::Frame:
				gl_ParseFrame(sc, name);
				break;
			case LightTag::Attenuate:
				// [rc4l] GZDoom attenuation flag: accept + consume its arg (legacy GL always
				// attenuates), so MBF21/GZDoom wads that set it -- e.g. Judgment -- still load.
				gl_ParseInt(sc);
				break;
			default:
				break;
			}
		}
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void gl_ReleaseLights()
{
   unsigned int i;

   for (i = 0; i < LightDefaults.Size(); i++)
   {
      delete LightDefaults[i];
   }

   LightAssociations.Clear();
   LightDefaults.Clear();
}

//==========================================================================
//
//
//
//==========================================================================

// these are the core types available in the *DEFS lump
static const char *CoreKeywords[]=
{
   "pointlight",
   "pulselight",
   "flickerlight",
   "flickerlight2",
   "sectorlight",
   "object",
   "clearlights",
   "shader",
   "clearshaders",
   "skybox",
   "glow",
   "brightmap",
   "disable_fullbright",
   "hardwareshader",
   "detail",
   "#include",
   "material",
   "fua_material",
   "lightsizefactor",
   "colorization",
   NULL
};


enum
{
   LIGHT_POINT,
   LIGHT_PULSE,
   LIGHT_FLICKER,
   LIGHT_FLICKER2,
   LIGHT_SECTOR,
   LIGHT_OBJECT,
   LIGHT_CLEAR,
   TAG_SHADER,
   TAG_CLEARSHADERS,
   TAG_SKYBOX,
   TAG_GLOW,
   TAG_BRIGHTMAP,
   TAG_DISABLE_FB,
   TAG_HARDWARESHADER,
   TAG_DETAIL,
   TAG_INCLUDE,
   TAG_MATERIAL,
   TAG_FUA_MATERIAL,
   TAG_LIGHTSIZEFACTOR,
   TAG_COLORIZATION,
};

//==========================================================================
//
// [Doruk-Aksoy] Skips a UZDoom colorization block (name, then key-value pairs in braces).
//
//==========================================================================
static void gl_SkipColorization(FScanner &sc)
{
	sc.MustGetString();
	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
	}
}


//==========================================================================
//
// This is only here so any shader definition for ZDoomGL can be skipped
// There is no functionality for this stuff!
//
//==========================================================================
bool gl_ParseShader(FScanner &sc)
{
	int  ShaderDepth = 0;

	if (sc.GetString())
	{
		char *tmp;

		tmp = strstr(sc.String, "{");
		while (tmp)
		{
			ShaderDepth++;
			tmp++;
			tmp = strstr(tmp, "{");
		}

		tmp = strstr(sc.String, "}");
		while (tmp)
		{
			ShaderDepth--;
			tmp++;
			tmp = strstr(tmp, "}");
		}

		if (ShaderDepth == 0) return true;
	}
	return false;
}

//==========================================================================
//
// Light associations per actor class
//
// Turn this inefficient mess into something that can be used at run time.
//
//==========================================================================

class FInternalLightAssociation
{
public:
	FInternalLightAssociation(FLightAssociation * asso);
	int Sprite() const { return m_sprite; }
	int Frame() const { return m_frame; }
	const FLightDefaults *Light() const { return m_AssocLight; }
protected:
	int m_sprite;
	int m_frame;
	FLightDefaults * m_AssocLight;
};

//==========================================================================
//
//
//
//==========================================================================

FInternalLightAssociation::FInternalLightAssociation(FLightAssociation * asso)
{

	m_AssocLight=NULL;
	for(unsigned int i=0;i<LightDefaults.Size();i++)
	{
		if (LightDefaults[i]->GetName() == asso->Light())
		{
			m_AssocLight = LightDefaults[i];
			break;
		}
	}

	m_sprite=-1;
	m_frame = -1;
	for (unsigned i = 0; i < sprites.Size (); ++i)
	{
		if (strncmp (sprites[i].name, asso->FrameName(), 4) == 0)
		{
			m_sprite = (int)i;
			break;
		}
	}

	// Only handle lights for full frames. 
	// I won't bother with special lights for single rotations
	// because there is no decent use for them!
	if (strlen(asso->FrameName())==5 || asso->FrameName()[5]=='0')
	{
		m_frame = toupper(asso->FrameName()[4])-'A';
	}
}


//==========================================================================
//
//
//
//==========================================================================

inline TDeletingArray<FInternalLightAssociation *> * gl_GetActorLights(AActor * actor)
{
	return (TDeletingArray<FInternalLightAssociation *>*)actor->lightassociations;
}

TDeletingArray< TDeletingArray<FInternalLightAssociation *> * > AssoDeleter;

//==========================================================================
//
// State lights
//
//==========================================================================
TArray<FName> ParsedStateLights;
TArray<FLightDefaults *> StateLights;



//==========================================================================
//
//
//
//==========================================================================

void gl_InitializeActorLights()
{
	// [rc4l] A restart (wad_reload) makes every class again with no lights, so the lists the last start made are freed here instead of at exit.
	AssoDeleter.DeleteAndClear();
	for(unsigned int i=0;i<LightAssociations.Size();i++)
	{
		const PClass * ti = PClass::FindClass(LightAssociations[i].ActorName());
		if (ti)
		{
			ti = GetRealType(ti);
			AActor * defaults = GetDefaultByType(ti);
			if (defaults)
			{
				FInternalLightAssociation * iasso = new FInternalLightAssociation(&LightAssociations[i]);

				if (!defaults->lightassociations)
				{
					TDeletingArray<FInternalLightAssociation*> *p =new TDeletingArray<FInternalLightAssociation*>;
					defaults->lightassociations = p;
					AssoDeleter.Push(p);
				}
				TDeletingArray<FInternalLightAssociation *> * lights = gl_GetActorLights(defaults);
				if (iasso->Light()==NULL)
				{
					// The definition was not valid.
					delete iasso;
				}
				else
				{
					lights->Push(iasso);
				}
			}
		}
	}
	// we don't need the parser data for the light associations anymore
	LightAssociations.Clear();
	LightAssociations.ShrinkToFit();

	StateLights.Resize(ParsedStateLights.Size()+1);
	for(unsigned i=0; i<ParsedStateLights.Size();i++)
	{
		if (ParsedStateLights[i] != NAME_None)
		{
			StateLights[i] = (FLightDefaults*)-1;	// something invalid that's not NULL.
			for(unsigned int j=0;j<LightDefaults.Size();j++)
			{
				if (LightDefaults[j]->GetName() == ParsedStateLights[i])
				{
					StateLights[i] = LightDefaults[j];
					break;
				}
			}
		}
		else StateLights[i] = NULL;
	}
	StateLights[StateLights.Size()-1] = NULL;	// terminator
	ParsedStateLights.Clear();
	ParsedStateLights.ShrinkToFit();
}


//==========================================================================
//
//
//
//==========================================================================

void gl_AttachLight(AActor *actor, unsigned int count, const FLightDefaults *lightdef)
{
	ADynamicLight *light;

		// I'm skipping the single rotations because that really doesn't make sense!
	if (count < actor->dynamiclights.Size()) 
	{
		light = barrier_cast<ADynamicLight*>(actor->dynamiclights[count]);
		assert(light != NULL);
	}
	else
	{
		light = Spawn<ADynamicLight>(actor->x, actor->y, actor->z, NO_REPLACE);
		light->target = actor;
		light->owned = true;
		actor->dynamiclights.Push(light);
	}
	light->flags2&=~MF2_DORMANT;
	lightdef->ApplyProperties(light);
}

//==========================================================================
//
// per-state light adjustment
//
//==========================================================================

void gl_SetActorLights(AActor *actor)
{
	// [BB] The server never displays dynamic lights.
	if ( zx::net::IsServer() )
		return;

	// [AK] Don't display dynamic lights on spectators.
	if (( actor->player != NULL ) && ( actor->player->bSpectating ))
		return;

	TArray<FInternalLightAssociation *> * l = gl_GetActorLights(actor);
	unsigned int count = 0;

	if (actor->state == NULL) return;
	if (l)
	{
		TArray<FInternalLightAssociation *> & LightAssociations=*l;
		ADynamicLight *lights, *tmpLight;
		unsigned int i;

		int sprite = actor->sprite;
		int frame = actor->frame;

		lights = tmpLight = NULL;


		for (i = 0; i < LightAssociations.Size(); i++)
		{
			if (LightAssociations[i]->Sprite() == sprite &&
				(LightAssociations[i]->Frame()==frame || LightAssociations[i]->Frame()==-1))
			{
				gl_AttachLight(actor, count++, LightAssociations[i]->Light());
			}
		}
	}
	if (count == 0 && actor->state->Light > 0)
	{
		for(int i= actor->state->Light; StateLights[i] != NULL; i++)
		{
			if (StateLights[i] != (FLightDefaults*)-1)
			{
				gl_AttachLight(actor, count++, StateLights[i]);
			}
		}
	}

	for(;count<actor->dynamiclights.Size();count++)
	{
		actor->dynamiclights[count]->flags2|=MF2_DORMANT;
		memset(actor->dynamiclights[count]->args, 0, sizeof(actor->args));
	}
}

//==========================================================================
//
// This is called before saving the game
//
//==========================================================================

void gl_DeleteAllAttachedLights()
{
	TThinkerIterator<AActor> it;
	AActor * a;
	ADynamicLight * l;

	while ((a=it.Next())) 
	{
		a->dynamiclights.Clear();
	}

	TThinkerIterator<ADynamicLight> it2;

	l=it2.Next();
	while (l) 
	{
		ADynamicLight * ll = it2.Next();
		if (l->owned) l->Destroy();
		l=ll;
	}


}

//==========================================================================
//
//
//
//==========================================================================

void gl_RecreateAllAttachedLights()
{
	TThinkerIterator<AActor> it;
	AActor * a;

	while ((a=it.Next())) 
	{
		gl_SetActorLights(a);
	}
}


//==========================================================================
// The actual light def parsing code is there.
// DoParseDefs is no longer called directly by ParseDefs, now it's called
// by LoadDynLightDefs, which wasn't simply integrated into ParseDefs
// because of the way the code needs to load two out of five lumps.
//==========================================================================
void gl_DoParseDefs(FScanner &sc, int workingLump)
{
	int recursion=0;
	int lump, type;

	// Get actor class name.
	while (true)
	{
		sc.SavePos();
		if (!sc.GetToken ())
		{
			return;
		}
		type = sc.MatchString(CoreKeywords);
		switch (type)
		{
		case TAG_INCLUDE:
			{
				sc.MustGetString();
				// This is not using sc.Open because it can print a more useful error message when done here
				lump = Wads.CheckNumForFullName(sc.String, true);
				if (lump==-1)
					sc.ScriptError("Lump '%s' not found", sc.String);

				FScanner newscanner(lump);
				gl_DoParseDefs(newscanner, lump);
				break;
			}
		case LIGHT_POINT:
			gl_ParseLight(sc, LightBlock::Point, PointLight);
			break;
		case LIGHT_PULSE:
			gl_ParseLight(sc, LightBlock::Pulse, PulseLight);
			break;
		case LIGHT_FLICKER:
			gl_ParseLight(sc, LightBlock::Flicker, FlickerLight);
			break;
		case LIGHT_FLICKER2:
			gl_ParseLight(sc, LightBlock::Flicker2, RandomFlickerLight);
			break;
		case LIGHT_SECTOR:
			gl_ParseLight(sc, LightBlock::Sector, SectorLight);
			break;
		// [Doruk-Aksoy] UZDoom's size factor for attenuated lights, read and not yet acted on.
		case TAG_LIGHTSIZEFACTOR:
			gl_ParseFloat(sc);
			break;
		// [Doruk-Aksoy] UZDoom's texture colourisation, which we lack, skipped so a UZDoom GLDEFS loads.
		case TAG_COLORIZATION:
			gl_SkipColorization(sc);
			break;
		case LIGHT_OBJECT:
			gl_ParseObject(sc);
			break;
		case LIGHT_CLEAR:
			gl_ReleaseLights();
			break;
		case TAG_SHADER:
			gl_ParseShader(sc);
			break;
		case TAG_CLEARSHADERS:
			break;
		case TAG_SKYBOX:
			gl_ParseSkybox(sc);
			break;
		case TAG_GLOW:
			gl_InitGlow(sc);
			break;
		case TAG_BRIGHTMAP:
			gl_ParseBrightmap(sc, workingLump);
			break;
		case TAG_HARDWARESHADER:
			zx::hwshaders::ParseHardwareShader(sc, workingLump);
			break;
		// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp: a texture's maps and shader in one Material block.
		case TAG_MATERIAL:
			zx::hwshaders::ParseMaterial(sc, workingLump);
			break;
		// [Doruk-Aksoy] features/actormaterials: a material DECORATE and ACS give an actor, with uniforms of its own.
		case TAG_FUA_MATERIAL:
			zx::hwshaders::ParseActorMaterial(sc);
			break;
		case TAG_DETAIL:
			gl_ParseDetailTexture(sc);
			break;
		case TAG_DISABLE_FB:
			{
				/* not implemented.
				sc.MustGetString();
				const PClass *cls = PClass::FindClass(sc.String);
				if (cls) GetDefaultByType(cls)->renderflags |= RF_NEVERFULLBRIGHT;
				*/
			}
			break;
		default:
			sc.ScriptError("Error parsing defs.  Unknown tag: %s.\n", sc.String);
			break;
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

void gl_LoadGLDefs(const char * defsLump)
{
	int workingLump, lastLump;

	lastLump = 0;
	while ((workingLump = Wads.FindLump(defsLump, &lastLump)) != -1)
	{
		FScanner sc(workingLump);
		gl_DoParseDefs(sc, workingLump);
	}
}


//==========================================================================
//
//
//
//==========================================================================

const char *gl_GameDefsLump()
{
	switch (gameinfo.gametype)
	{
	case GAME_Heretic:
		return "HTICDEFS";
	case GAME_Hexen:
		return "HEXNDEFS";
	case GAME_Strife:
		return "STRFDEFS";
	case GAME_Doom:
		return "DOOMDEFS";
	case GAME_Chex:
		return "CHEXDEFS";
	default: // silence GCC
		return NULL;
	}
}

void gl_ParseDefs()
{
	const char *defsLump = gl_GameDefsLump();

	atterm( gl_ReleaseLights );
	gl_ReleaseLights();
	// [Doruk-Aksoy] A restart reads GLDEFS again, so the post-process shaders it declares start over.
	zx::postprocess::ClearPostProcessShaders(defsLump);
	zx::actormaterials::ClearDeclarations();
	gl_ParseVavoomSkybox();
	if (defsLump != NULL) gl_LoadGLDefs(defsLump);
	gl_LoadGLDefs("GLDEFS");
	gl_InitializeActorLights();
	zx::postprocess::NotePostProcessRenderer(zx::hwrender::BackendOnly());
}


//==========================================================================
//
//
//
//==========================================================================

void AddStateLight(FState *State, const char *lname)
{
	if (State->Light == 0)
	{
		ParsedStateLights.Push(NAME_None);
		State->Light = ParsedStateLights.Push(FName(lname));
	}
	else
	{
		ParsedStateLights.Push(FName(lname));
	}
}

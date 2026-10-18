#ifndef __A_SHAREDGLOBAL_H__
#define __A_SHAREDGLOBAL_H__

#include "info.h"
#include "actor.h"
// [AK] New #includes.
#include "a_pickups.h"

class FDecalTemplate;
struct vertex_t;
struct side_t;
struct F3DFloor;

void P_SpawnDirt (AActor *actor, fixed_t radius);

// [rc4l] features/decals makes every decal, through the one class these befriend, so no caller can place a mark around its rules.
namespace zx { namespace decals { class Maker; } }

class DBaseDecal : public DThinker
{
	DECLARE_CLASS (DBaseDecal, DThinker)
	HAS_OBJECT_POINTERS
	friend class zx::decals::Maker;
protected:
	DBaseDecal ();
	DBaseDecal (fixed_t z);
	DBaseDecal (int statnum, fixed_t z);
	DBaseDecal (const AActor *actor);
	DBaseDecal (const DBaseDecal *basis);

public:
	void Serialize (FArchive &arc);
	void Destroy ();
	FTextureID StickToWall (side_t *wall, fixed_t x, fixed_t y, F3DFloor * ffloor);
	// [rc4l] The texture of the tier a decal at (x, y, z) on wall sticks to, the one features/decals judges; invalid in a two-sided line's open span.
	static FTextureID TierTexture (const side_t *wall, fixed_t x, fixed_t y, fixed_t z, const F3DFloor *ffloor);
	fixed_t GetRealZ (const side_t *wall) const;
	// [rc4l] features/levelmesh: the same resolution without a decal object, so the backend can ask
	// again every frame instead of once at spawn. See RealZOnWall.
	static fixed_t RealZOnWall (const side_t *wall, fixed_t z, DWORD renderFlags);
	void SetShade (DWORD rgb);
	void SetShade (int r, int g, int b);
	void Spread (const FDecalTemplate *tpl, side_t *wall, fixed_t x, fixed_t y, fixed_t z, F3DFloor * ffloor);
	void GetXY (side_t *side, fixed_t &x, fixed_t &y) const;

	static void SerializeChain (FArchive &arc, DBaseDecal **firstptr);

	DBaseDecal *WallNext, **WallPrev;

	fixed_t LeftDistance;
	fixed_t Z;
	fixed_t ScaleX, ScaleY;
	fixed_t Alpha;
	DWORD AlphaColor;
	int Translation;
	FTextureID PicNum;
	DWORD RenderFlags;
	FRenderStyle RenderStyle;
	sector_t * Sector;	// required for 3D floors

protected:
	virtual DBaseDecal *CloneSelf (const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z, side_t *wall, F3DFloor * ffloor) const;
	void CalcFracPos (side_t *wall, fixed_t x, fixed_t y);
	void Remove ();

	static void SpreadLeft (fixed_t r, vertex_t *v1, side_t *feelwall, F3DFloor *ffloor);
	static void SpreadRight (fixed_t r, side_t *feelwall, fixed_t wallsize, F3DFloor *ffloor);
};

class DImpactDecal : public DBaseDecal
{
	DECLARE_CLASS (DImpactDecal, DBaseDecal)
	friend class zx::decals::Maker;
public:
	void Destroy ();

	void Serialize (FArchive &arc);
	static void SerializeTime (FArchive &arc);

protected:
	DImpactDecal ();
	DImpactDecal (fixed_t z);
	DBaseDecal *CloneSelf (const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z, side_t *wall, F3DFloor * ffloor) const;
	static void CheckMax ();
};

class ATeleportFog : public AActor
{
	DECLARE_CLASS (ATeleportFog, AActor)
public:
	void PostBeginPlay ();
};

class ASkyViewpoint : public AActor
{
	DECLARE_CLASS (ASkyViewpoint, AActor)
	HAS_OBJECT_POINTERS
public:
	void Serialize (FArchive &arc);
	void BeginPlay ();
	void Destroy ();
	bool bInSkybox;
	bool bAlways;
	TObjPtr<ASkyViewpoint> Mate;
};

class AStackPoint : public ASkyViewpoint
{
	DECLARE_CLASS (AStackPoint, ASkyViewpoint)
public:
	void BeginPlay ();
};

class DFlashFader : public DThinker
{
	DECLARE_CLASS (DFlashFader, DThinker)
	HAS_OBJECT_POINTERS
public:
	DFlashFader (float r1, float g1, float b1, float a1,
				 float r2, float g2, float b2, float a2,
				 float time, AActor *who);
	void Destroy ();
	void Serialize (FArchive &arc);
	void Tick ();
	AActor *WhoFor() { return ForWho; }
	void Cancel ();

protected:
	float Blends[2][4];
	int TotalTics;
	int StartTic;
	TObjPtr<AActor> ForWho;

	void SetBlend (float time);
	DFlashFader ();
};

// [rc4l] uzdoom@7050d0322 .. uzdoom@e29b8b209 -- per-axis quakes, scaling flags and QF_WAVE.
enum
{
	QF_RELATIVE =		1,
	QF_SCALEDOWN =		1 << 1,
	QF_SCALEUP =		1 << 2,
	QF_MAX =			1 << 3,
	QF_FULLINTENSITY =	1 << 4,
	QF_WAVE =			1 << 5,
};

struct FQuakeJiggers
{
	int IntensityX, IntensityY, IntensityZ;
	int RelIntensityX, RelIntensityY, RelIntensityZ;
	int OffsetX, OffsetY, OffsetZ;
	int RelOffsetX, RelOffsetY, RelOffsetZ;
};

// [rc4l] What a quake's shake is worked out from, taken between tics by features/gamecopy so a frame shakes the view at its own point through the tic.
struct FQuakeState
{
	int Countdown, CountdownStart, Flags;
	int IntensityX, IntensityY, IntensityZ;	// [rc4l] raw integer intensities
	float WaveSpeedX, WaveSpeedY, WaveSpeedZ;

	int GetModIntensity(int intensity) const;
	// [rc4l] Reads r_TicFrac, the frame's point through the tic.
	int GetModWave(double waveMultiplier) const;
};

class DEarthquake : public DThinker
{
	DECLARE_CLASS (DEarthquake, DThinker)
	HAS_OBJECT_POINTERS
public:
	DEarthquake(AActor *center, int intensityX, int intensityY, int intensityZ, int duration,
		int damrad, int tremrad, FSoundID quakesfx, int flags, 
		double waveSpeedX, double waveSpeedY, double waveSpeedZ);

	void Serialize (FArchive &arc);
	void Tick ();
	TObjPtr<AActor> m_Spot;
	fixed_t m_TremorRadius, m_DamageRadius;
	int m_Countdown;
	int m_CountdownStart;
	FSoundID m_QuakeSFX;
	int m_Flags;
	fixed_t m_IntensityX, m_IntensityY, m_IntensityZ;
	float m_WaveSpeedX, m_WaveSpeedY, m_WaveSpeedZ;

	// [rc4l] upstream declares these fixed_t and defines them int-valued, which only compiles because
	// its fixed_t IS int. Ours is the strong zx::Fixed, so they are int here -- matching FQuakeJiggers,
	// which stores ints -- and the conversion happens once, at the QuakePower call in r_utility.
	FQuakeState State() const;
	int GetModIntensity(int intensity) const { return State().GetModIntensity(intensity); }
	int GetModWave(double waveMultiplier) const { return State().GetModWave(waveMultiplier); }

	// [rc4l] The quakes shaking a viewer, in thinker order, none for a noclipping player.
	static void StaticGetQuakes(AActor *viewer, TArray<FQuakeState> &quakes);
	// [rc4l] Their combined shake into the pre-zeroed jiggers, and how many there are.
	static int StaticQuakeIntensities(const TArray<FQuakeState> &quakes, FQuakeJiggers &jiggers);
	static int StaticGetQuakeIntensities(AActor *viewer, FQuakeJiggers &jiggers);

private:
	DEarthquake ();
};

class AMorphProjectile : public AActor
{
	DECLARE_CLASS (AMorphProjectile, AActor)
public:
	int DoSpecialDamage (AActor *target, int damage, FName damagetype);
	void Serialize (FArchive &arc);

	FNameNoInit	PlayerClass, MonsterClass, MorphFlash, UnMorphFlash;
	int Duration, MorphStyle;
};

class AMorphedMonster : public AActor
{
	DECLARE_CLASS (AMorphedMonster, AActor)
	HAS_OBJECT_POINTERS
public:
	void Tick ();
	void Serialize (FArchive &arc);
	void Die (AActor *source, AActor *inflictor, int dmgflags);
	void Destroy ();

	TObjPtr<AActor> UnmorphedMe;
	int UnmorphTime, MorphStyle;
	const PClass *MorphExitFlash;
	ActorFlags FlagsSave;
};

class AMapMarker : public AActor
{
	DECLARE_CLASS(AMapMarker, AActor)
public:
	void BeginPlay ();
	void Activate (AActor *activator);
	void Deactivate (AActor *activator);
};

class AFastProjectile : public AActor
{
	DECLARE_CLASS(AFastProjectile, AActor)
public:
	void Tick ();
	virtual void Effect();
};

// [BC/AK] Start of Skulltag/Zandronum actor classes.
class ATeamItem : public AInventory
{
	DECLARE_CLASS (ATeamItem, AInventory)
public:
	virtual bool ShouldRespawn ();
	virtual bool TryPickup (AActor *&toucher);
	virtual bool HandlePickup (AInventory *item);
	virtual int AllowPickup (AActor *toucher);
	virtual void AnnouncePickup (AActor *toucher);
	virtual void DisplayTaken (AActor *toucher);
	virtual void Return (AActor *returner);
	virtual void AnnounceReturn ();
	virtual void DisplayReturn (AActor *returner);
	void MarkTaken (bool taken);
	void ResetReturnTicks ();

	static void Drop (player_t *player, unsigned int team);

protected:
	virtual const char *GetType () { return "item"; }
};

class AFlag : public ATeamItem
{
	DECLARE_CLASS (AFlag, ATeamItem)
public:
	virtual bool HandlePickup (AInventory *item);
	virtual int AllowPickup (AActor *toucher);

protected:
	virtual const char *GetType () { return "flag"; }
};

class AWhiteFlag : public AFlag
{
	DECLARE_CLASS (AWhiteFlag, AFlag)
public:
	virtual bool HandlePickup (AInventory *item);
	virtual int AllowPickup (AActor *toucher);
	virtual void AnnouncePickup (AActor *toucher);
	virtual void DisplayTaken (AActor *toucher);
	virtual void Return (AActor *returner);
	virtual void AnnounceReturn ();
	virtual void DisplayReturn (AActor *returner);
};

class ASkull : public ATeamItem
{
	DECLARE_CLASS (ASkull, ATeamItem)
public:
	virtual int AllowPickup (AActor *toucher);

protected:
	virtual const char *GetType () { return "skull"; }
};

class AFloatyIcon : public AActor
{
	DECLARE_CLASS (AFloatyIcon, AActor)
public:
	virtual void Serialize (FArchive &arc);
	virtual void BeginPlay ();
	virtual void Tick ();

	void SetTracer (AActor *pTracer);

	LONG lTick;
	bool bTeamItemFloatyIcon;
	unsigned int currentSprite;

protected:
	void CopyTracerTranslucency ();
};


#endif //__A_SHAREDGLOBAL_H__

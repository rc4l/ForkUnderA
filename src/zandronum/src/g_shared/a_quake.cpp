#include "templates.h"
#include "doomtype.h"
#include "doomstat.h"
#include "p_local.h"
#include "actor.h"
#include "m_bbox.h"
#include "m_random.h"
#include "s_sound.h"
#include "a_sharedglobal.h"
#include "statnums.h"
#include "farchive.h"
// [BB] New #includes.
#include "network.h"
#include "sv_commands.h"

static FRandom pr_quake ("Quake");

IMPLEMENT_POINTY_CLASS (DEarthquake)
 DECLARE_POINTER (m_Spot)
END_POINTERS

//==========================================================================
//
// DEarthquake :: DEarthquake private constructor
//
//==========================================================================

DEarthquake::DEarthquake()
: DThinker(STAT_EARTHQUAKE)
{
}

//==========================================================================
//
// DEarthquake :: DEarthquake public constructor
//
//==========================================================================

DEarthquake::DEarthquake (AActor *center, int intensityX, int intensityY, int intensityZ, int duration,
						  int damrad, int tremrad, FSoundID quakesound, int flags, 
						  double waveSpeedX, double waveSpeedY, double waveSpeedZ)
						  : DThinker(STAT_EARTHQUAKE)
{
	// [BC] If we're the server, tell clients to do the earthquake.
	// [rc4l] Wave speeds are NOT sent: SERVERCOMMANDS_Earthquake carries per-axis intensity, duration,
	// tremor radius, sound and flags, and adding three more fixed_t fields is a further wire change.
	// A client therefore renders a QF_WAVE quake without its wave motion -- recorded in the row.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( center ))
		SERVERCOMMANDS_Earthquake( center, intensityX, intensityY, intensityZ, duration, tremrad, quakesound, flags );

	m_QuakeSFX = quakesound;
	m_Spot = center;
	// Radii are specified in tile units (64 pixels)
	m_DamageRadius = damrad << FRACBITS;
	m_TremorRadius = tremrad << FRACBITS;
	m_IntensityX = intensityX << FRACBITS;
	m_IntensityY = intensityY << FRACBITS;
	m_IntensityZ = intensityZ << FRACBITS;
	m_CountdownStart = duration;
	m_Countdown = duration;
	m_Flags = flags;
	m_WaveSpeedX = (float)waveSpeedX;
	m_WaveSpeedY = (float)waveSpeedY;
	m_WaveSpeedZ = (float)waveSpeedZ;
}

//==========================================================================
//
// DEarthquake :: Serialize
//
//==========================================================================

void DEarthquake::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << m_Spot << m_IntensityX << m_Countdown
		<< m_TremorRadius << m_DamageRadius
		<< m_QuakeSFX;
	if (SaveVersion < 4519)
	{
		m_IntensityY = m_IntensityX;
		m_IntensityZ = 0;
		m_Flags = 0;
	}
	else
	{
		arc << m_IntensityY << m_IntensityZ << m_Flags;
	}
	if (SaveVersion < 4520)
	{
		m_CountdownStart = 0;
	}
	else
	{
		arc << m_CountdownStart;
	}
	if (SaveVersion < 4521)
	{
		m_WaveSpeedX = m_WaveSpeedY = m_WaveSpeedZ = 0;
		m_IntensityX <<= FRACBITS;
		m_IntensityY <<= FRACBITS;
		m_IntensityZ <<= FRACBITS;
	}
	else
	{
		arc << m_WaveSpeedX << m_WaveSpeedY << m_WaveSpeedZ;
	}
}

//==========================================================================
//
// DEarthquake :: Tick
//
// Deals damage to any players near the earthquake and makes sure it's
// making noise.
//
//==========================================================================

void DEarthquake::Tick ()
{
	int i;

	if (m_Spot == NULL)
	{
		Destroy ();
		return;
	}
	
	if (!S_IsActorPlayingSomething (m_Spot, CHAN_BODY, m_QuakeSFX))
	{
		S_Sound (m_Spot, CHAN_BODY | CHAN_LOOP, m_QuakeSFX, 1, ATTN_NORM);
	}
	if (m_DamageRadius > 0)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !(players[i].cheats & CF_NOCLIP))
			{
				AActor *victim = players[i].mo;
				fixed_t dist;

				dist = P_AproxDistance (victim->x - m_Spot->x, victim->y - m_Spot->y);
				// Check if in damage radius
				if (dist < m_DamageRadius && victim->z <= victim->floorz)
				{
					if (pr_quake() < 50)
					{
						P_DamageMobj (victim, NULL, NULL, pr_quake.HitDice (1), NAME_None);
					}
					// Thrust player around
					angle_t an = victim->angle + ANGLE_1*pr_quake();
					if (m_IntensityX == m_IntensityY)
					{ // Thrust in a circle
						P_ThrustMobj (victim, an, m_IntensityX << (FRACBITS-1));
					}
					else
					{ // Thrust in an ellipse
						an >>= ANGLETOFINESHIFT;
						// So this is actually completely wrong, but it ought to be good
						// enough. Otherwise, I'd have to use tangents and square roots.
						victim->velx += FixedMul(m_IntensityX << (FRACBITS-1), finecosine[an]);
						victim->vely += FixedMul(m_IntensityY << (FRACBITS-1), finesine[an]);
					}
				}
			}
		}
	}
	
	if (--m_Countdown == 0)
	{
		if (S_IsActorPlayingSomething(m_Spot, CHAN_BODY, m_QuakeSFX))
		{
			S_StopSound(m_Spot, CHAN_BODY);
		}
		Destroy();
	}
}

int FQuakeState::GetModWave(double waveMultiplier) const
{
	//QF_WAVE converts intensity into amplitude and unlocks a new property, the wave length.
	//This is, in short, waves per second (full cycles, mind you, from 0 to 360.)
	//Named waveMultiplier because that's as the name implies: adds more waves per second.

	double time = Countdown - FIXED2DBL(r_TicFrac);
	return FLOAT2FIXED(sin(waveMultiplier * time * (M_PI * 2 / TICRATE)));
}

//==========================================================================
//
// DEarthquake :: GetModIntensity
//
// Given a base intensity, modify it according to the quake's flags.
//
//==========================================================================

int FQuakeState::GetModIntensity(int intensity) const
{
	assert(CountdownStart >= Countdown);
	intensity += intensity;		// always doubled

	if (Flags & (QF_SCALEDOWN | QF_SCALEUP))
	{
		int scalar;
		if ((Flags & (QF_SCALEDOWN | QF_SCALEUP)) == (QF_SCALEDOWN | QF_SCALEUP))
		{
			scalar = (Flags & QF_MAX) ? MAX(Countdown, CountdownStart - Countdown)
				: MIN(Countdown, CountdownStart - Countdown);

			if (Flags & QF_FULLINTENSITY)
			{
				scalar *= 2;
			}
		}
		else if (Flags & QF_SCALEDOWN)
		{
			scalar = Countdown;
		}
		else			// QF_SCALEUP
		{
			scalar = CountdownStart - Countdown;
		}
		assert(CountdownStart > 0);
		intensity = Scale(intensity, scalar, CountdownStart);
	}
	return intensity;
}

//==========================================================================
//
// DEarthquake::StaticGetQuakeIntensity
//
// Searches for all quakes near the victim and returns their combined
// intensity.
//
// Pre: jiggers was pre-zeroed by the caller.
//
//==========================================================================

FQuakeState DEarthquake::State() const
{
	FQuakeState s;
	s.Countdown = m_Countdown;
	s.CountdownStart = m_CountdownStart;
	s.Flags = m_Flags;
	s.IntensityX = m_IntensityX.Raw();
	s.IntensityY = m_IntensityY.Raw();
	s.IntensityZ = m_IntensityZ.Raw();
	s.WaveSpeedX = m_WaveSpeedX;
	s.WaveSpeedY = m_WaveSpeedY;
	s.WaveSpeedZ = m_WaveSpeedZ;
	return s;
}

void DEarthquake::StaticGetQuakes(AActor *victim, TArray<FQuakeState> &quakes)
{
	quakes.Clear();
	if (victim->player != NULL && (victim->player->cheats & CF_NOCLIP))
	{
		return;
	}

	TThinkerIterator<DEarthquake> iterator(STAT_EARTHQUAKE);
	DEarthquake *quake;

	while ( (quake = iterator.Next()) != NULL)
	{
		if (quake->m_Spot != NULL)
		{
			fixed_t dist = P_AproxDistance (victim->x - quake->m_Spot->x,
				victim->y - quake->m_Spot->y);
			if (dist < quake->m_TremorRadius)
			{
				quakes.Push(quake->State());
			}
		}
	}
}

int DEarthquake::StaticQuakeIntensities(const TArray<FQuakeState> &quakes, FQuakeJiggers &jiggers)
{
	for (unsigned i = 0; i < quakes.Size(); i++)
	{
		const FQuakeState *quake = &quakes[i];
		// [rc4l] The intensities are raw integers, which upstream kept in fixed_t members, so the maths below stays int-valued.
		int x = quake->GetModIntensity(quake->IntensityX);
		int y = quake->GetModIntensity(quake->IntensityY);
		int z = quake->GetModIntensity(quake->IntensityZ);
		if (!(quake->Flags & QF_WAVE))
		{
			if (quake->Flags & QF_RELATIVE)
			{
				jiggers.RelIntensityX = MAX(x, jiggers.RelIntensityX);
				jiggers.RelIntensityY = MAX(y, jiggers.RelIntensityY);
				jiggers.RelIntensityZ = MAX(z, jiggers.RelIntensityZ);
			}
			else
			{
				jiggers.IntensityX = MAX(x, jiggers.IntensityX);
				jiggers.IntensityY = MAX(y, jiggers.IntensityY);
				jiggers.IntensityZ = MAX(z, jiggers.IntensityZ);
			}
		}
		else
		{
			int mx = FixedMul(fixed_t::FromRaw(x), fixed_t::FromRaw(quake->GetModWave(quake->WaveSpeedX))).Raw();
			int my = FixedMul(fixed_t::FromRaw(y), fixed_t::FromRaw(quake->GetModWave(quake->WaveSpeedY))).Raw();
			int mz = FixedMul(fixed_t::FromRaw(z), fixed_t::FromRaw(quake->GetModWave(quake->WaveSpeedZ))).Raw();

			// [RH] This only gives effect to the last sine quake. I would
			// prefer if some way was found to make multiples coexist
			// peacefully, but just summing them together is undesirable
			// because they could cancel each other out depending on their
			// relative phases.
			if (quake->Flags & QF_RELATIVE)
			{
				jiggers.RelOffsetX = mx;
				jiggers.RelOffsetY = my;
				jiggers.RelOffsetZ = mz;
			}
			else
			{
				jiggers.OffsetX = mx;
				jiggers.OffsetY = my;
				jiggers.OffsetZ = mz;
			}
		}
	}
	return (int)quakes.Size();
}

int DEarthquake::StaticGetQuakeIntensities(AActor *victim, FQuakeJiggers &jiggers)
{
	TArray<FQuakeState> quakes;
	StaticGetQuakes(victim, quakes);
	return StaticQuakeIntensities(quakes, jiggers);
}

//==========================================================================
//
// P_StartQuake
//
//==========================================================================

bool P_StartQuakeXYZ(AActor *activator, int tid, int intensityX, int intensityY, int intensityZ, int duration,
	int damrad, int tremrad, FSoundID quakesfx, int flags,
	double waveSpeedX, double waveSpeedY, double waveSpeedZ)
{
	AActor *center;
	bool res = false;

	if (intensityX)		intensityX = clamp(intensityX, 1, 9);
	if (intensityY)		intensityY = clamp(intensityY, 1, 9);
	if (intensityZ)		intensityZ = clamp(intensityZ, 1, 9);

	if (tid == 0)
	{
		if (activator != NULL)
		{
			new DEarthquake(activator, intensityX, intensityY, intensityZ, duration, damrad, tremrad,
				quakesfx, flags, waveSpeedX, waveSpeedY, waveSpeedZ);
			return true;
		}
	}
	else
	{
		FActorIterator iterator (tid);
		while ( (center = iterator.Next ()) )
		{
			res = true;
			new DEarthquake(center, intensityX, intensityY, intensityZ, duration, damrad, tremrad,
				quakesfx, flags, waveSpeedX, waveSpeedY, waveSpeedZ);
		}
	}
	
	return res;
}

bool P_StartQuake(AActor *activator, int tid, int intensity, int duration, int damrad, int tremrad, FSoundID quakesfx)
{	//Maintains original behavior by passing 0 to intensityZ, and flags.
	return P_StartQuakeXYZ(activator, tid, intensity, intensity, 0, duration, damrad, tremrad, quakesfx, 0, 0, 0, 0);
}

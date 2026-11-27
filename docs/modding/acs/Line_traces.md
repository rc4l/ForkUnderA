# Line traces in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX; elsewhere they return 0. A hit actor
> comes back as an [actor handle](Actor_handles.md).

| Number | Function | Returns |
|---|---|---|
| 24050 | `fua_TracerCreate()` | a tracer id, or 0 when the script already holds 16 |
| 24051 | `fua_TracerFree(int t)` | nothing; also happens when the script ends |
| 24052 | `fua_TracerClear(int t)` | nothing; drops the tracer's rules, callback and hits |
| 24053 | `fua_TraceRule(int t, int hit, int action [, int filter [, a [, b]]])` | 1 once added; 0 for a bad call, an unknown class, flag or texture, or a handle to no actor |
| 24054 | `fua_TraceCallback(int t, int function(int, int)? fn [, int ctx])` | 1 once set; `null` clears it; 0 for no tracer |
| 24055 | `fua_LineTrace(int t, int h, fixed angle, fixed distance, fixed pitch [, int flags [, fixed offsetz [, fixed offsetforward [, fixed offsetside]]]])` | the hit type, `TRACE_HitNone` (0) for nothing |
| 24056 | `fua_Trace(int t, fixed x, fixed y, fixed z, fixed dx, fixed dy, fixed dz, fixed distance [, int flags [, int ignore]])` | how many hits the rules kept |
| 24057 | `fua_TraceHitCount(int t)` | how many hits the tracer kept from its last trace |
| 24058 | `fua_TraceResult(int t, int prop [, int hit])` | a `TR_` property of a hit, or of the trace's end with no `hit` |
| 24059 | `fua_TraceResultStr(int t, int prop [, int hit])` | the hit texture's name for `TR_HitTexture`, "" otherwise |
| 24060 | `fua_LineInfo(int line, int prop)` | an `LI_` property of a line by index, 0 for none |
| 24061 | `fua_SectorInfo(int sector, int prop)` | an `SI_` property of a sector by index, 0 for none |

## Usage

`fua_LineTrace` is ZScript's `Actor.LineTrace`, with a tracer `t` in front that keeps the result. It
traces from the actor `h` (0 for the activator), at its feet plus `offsetz`, with `offsetforward` and
`offsetside` turned by `angle`. Angles and pitches are ACS angles (1.0 a full turn); a positive pitch
looks down.

```
int t = fua_TracerCreate();
if( fua_LineTrace( t, 0, GetActorAngle( 0 ), 2048.0, GetActorPitch( 0 ), 0, 41.0 ) == TRACE_HitWall )
{
    int x = fua_TraceResult( t, TR_HitX );
    int y = fua_TraceResult( t, TR_HitY );
    int line = fua_TraceResult( t, TR_HitLine );
    Log( s: "wall ", s: fua_TraceResultStr( t, TR_HitTexture ), s: " of line ", d: line,
         s: ", special ", d: fua_LineInfo( line, LI_SPECIAL ) );
}
```

| Flag | Value | Effect |
|---|---|---|
| `TRF_ABSPOSITION` | 1 | start at (`offsetforward`, `offsetside`, `offsetz`) on the map |
| `TRF_ABSOFFSET` | 2 | offsets along the map's axes, not turned by `angle` |
| `TRF_THRUSPECIES` | 4 | pass actors of the tracing actor's species |
| `TRF_THRUACTORS` | 8 | pass every actor |
| `TRF_THRUBLOCK` | 16 | pass lines that block everything |
| `TRF_THRUHITSCAN` | 32 | pass lines that block hitscans |
| `TRF_NOSKY` | 64 | a sky hit counts as nothing |
| `TRF_ALLACTORS` | 128 | stop at any actor, not only shootable ones |
| `TRF_SOLIDACTORS` | 256 | stop at solid actors too |
| `TRF_BLOCKUSE` | 512 | stop at lines that block use |
| `TRF_BLOCKSELF` | 1024 | stop at lines that would block the tracing actor's own movement |
| `TRF_FUA_PHYSICALSIZE` | 1048576 | measure actors by their size, not their hit size |

| Property | Value |
|---|---|
| `TR_HitType` | `TRACE_HitNone`, `_HitFloor`, `_HitCeiling`, `_HitWall` or `_HitActor` (0-4) |
| `TR_HitActor` | the hit actor's handle, 0 once it is gone |
| `TR_HitX`, `TR_HitY`, `TR_HitZ` | where the trace stopped (its end for nothing) |
| `TR_DirX`, `TR_DirY`, `TR_DirZ` | the trace's direction, unit length |
| `TR_Distance`, `TR_Fraction` | how far it went, and that as a fraction of `distance` |
| `TR_HitLine`, `TR_LineId` | the wall's line index (-1 for none) and its first line ID |
| `TR_LineSide`, `TR_LinePart` | front 0 or back 1; upper 0, middle 1 or lower 2 |
| `TR_HitSector`, `TR_SectorTag` | the sector index where it stopped (-1 for none) and its first tag |
| `TR_SectorPlane` | 1 for a ceiling, 0 otherwise |
| `TR_Hit3DFloor` | the hit 3D floor's control sector, -1 for none |
| `TR_CrossedWater`, `TR_Crossed3DWater` | the Boom deep-water sector, or the swimmable 3D floor's control sector, passed on the way; -1 for none |
| `TR_HitTexture` | the texture hit, through `fua_TraceResultStr` |

| `LI_` | | `SI_` | |
|---|---|---|---|
| `LI_FLAGS` | the line's flags | `SI_FLOOR` | floor height |
| `LI_SPECIAL`, `LI_ARG0`-`LI_ARG4` | its special and arguments | `SI_CEILING` | ceiling height |
| `LI_ID` | its first line ID | `SI_LIGHT` | light level |
| `LI_FRONTSECTOR`, `LI_BACKSECTOR` | its sectors' indices, -1 for none | `SI_SPECIAL` | special |
| `LI_ACTIVATION` | how its special is set off | `SI_TAG` | first tag |
| `LI_ALPHA` | its translucency (1.0 opaque) | `SI_FLAGS` | sector flags |
| `LI_X1`, `LI_Y1`, `LI_X2`, `LI_Y2` | its ends | `SI_DAMAGE` | damage a tic |

- **ACS names lines and sectors by ID and tag,** which needn't be unique or set, so traces give the index
  too, for `fua_LineInfo`, `fua_SectorInfo` and the existing line and sector functions through the ID
  and tag.
- **Actors are hit at their hit size,** the extent hitscans use, so a trace hits what a bullet would.
  `TRF_FUA_PHYSICALSIZE` measures them by their movement size instead.
- **A trace never sets off a line's special.**
- **Positions past ±32767 units lose their high bits,** as `GetActorX` does.
- **A tracer belongs to its script.** Each script holds up to 16; another script's id, or a freed one,
  reads nothing. Tracers are saved with their script, hits included. Tracer and query ids come from one
  counter, so they never meet.
- **Each machine traces its own world;** nothing is sent.
- Left out of UZDoom's `LineTrace`: `TRF_BLOCKSELF`'s test of `BLOCKASPLAYER`, a flag ZandroX doesn't have,
  and the portal count, as ZandroX has no line portals.

## Tracer rules

`fua_Trace` is ZScript's `LineTracer.Trace`: a trace from a map point along a direction (any length, made
unit length), where the tracer's rules decide each hit in place of a `TraceCallback`. Each hit is checked
against the rules in the order they were added and the first match decides; with no match the trace
stops there, as ZScript's default callback does. It returns how many hits the rules kept.

```
int t = fua_TracerCreate();
fua_TraceRule( t, TR_ACTOR, TRACE_Record, FQ_SHOOTABLE ); // keep every shootable actor and go on
fua_TraceRule( t, TR_ACTOR, TRACE_Skip );                 // pass anything else
fua_TraceRule( t, TR_WALL, TRACE_Stop );                  // the first wall ends it
int n = fua_Trace( t, x, y, z, dx, dy, dz, 4096.0, 0, fua_GetActorHandle( 0 ) );
for( int i = 0; i < n; i++ )
    if( fua_TraceResult( t, TR_HitType, i ) == TRACE_HitActor )
    {
        fua_SetActivatorHandle( fua_TraceResult( t, TR_HitActor, i ) );
        Thing_Damage2( 0, 20, "Normal" );
    }
```

| Hit kind | Matches |
|---|---|
| `TR_ANY` (64) | every hit |
| `TR_FLOOR`, `TR_CEILING` (65, 66) | a floor or ceiling |
| `TR_WALL` (67) | a wall, a line that blocks the trace, or with `TRF_FUA_ALLLINES` any line crossed |
| `TR_ACTOR` (68) | an actor |
| `TR_3DFLOOR` (69) | a 3D floor's side, which is not a `TR_WALL` |

| Action | Value | Effect |
|---|---|---|
| `TRACE_Stop` | 0 | keep the hit and end the trace there |
| `TRACE_Continue` | 1 | go on; the trace ends on this hit if nothing follows |
| `TRACE_Skip` | 2 | go on as if it weren't there |
| `TRACE_Abort` | 3 | end the trace with nothing, every kept hit dropped |
| `TRACE_Record` | 4 | keep the hit and go on |
| `TRACE_Callback` | 5 | let the tracer's callback function decide |

A rule's filter is any `FQ_` actor filter of [queries](Actor_queries.md) but `FQ_2D` and `FQ_EDGES`, or one
of these for lines, floors and ceilings; `b` 0 turns a `TQ_` filter round:

| Filter | Argument | Matches |
|---|---|---|
| `TQ_LINEFLAG` (48) | line flags | a line with all of them set |
| `TQ_SPECIAL` (49) | special | a line with that special |
| `TQ_TEXTURE` (50) | texture name | a wall, floor or ceiling showing that texture |

- **A floor or ceiling always ends the trace,** since nothing lies past it; `TRACE_Stop` and
  `TRACE_Record` keep it, and `TRACE_Abort` still drops everything.
- **With `TRF_NOSKY`, the sky ends the trace and is never kept.**
- **`flags` takes the `TRF_` flags that choose what is hit:** `TRF_ALLACTORS`, `TRF_SOLIDACTORS`,
  `TRF_BLOCKUSE`, `TRF_THRUBLOCK`, `TRF_THRUHITSCAN`, `TRF_NOSKY`, `TRF_FUA_PHYSICALSIZE`, and
  `TRF_FUA_ALLLINES` (2097152), which reports every line crossed so `TQ_` filters can pick them out.
  `TRF_BLOCKSELF` counts only with an `ignore` actor to be blocked as; the rules do what
  `TRF_THRUACTORS` and `TRF_THRUSPECIES` do.
- **`ignore` is a handle the trace passes through,** such as the shooter, which a trace started inside it
  would otherwise hit first.
- **A tracer holds up to 32 rules,** kept until `fua_TracerClear` and saved with it. `fua_LineTrace`
  ignores them and keeps them for the next `fua_Trace`.

## Callback functions

A rule's `TRACE_Callback` hands its hits to a function the script names with `fua_TraceCallback`, for
decisions rules can't express. The function takes the tracer and `ctx` and returns an action;
`fua_TraceResult( t, prop )` with no hit reads the hit it decides, and `fua_TraceHitCount` the hits kept
so far. zt-bcc checks the function's shape.

```
int power;

int Pierce( int t, int ctx )
{
    power -= fua_GetField( fua_TraceResult( t, TR_HitActor ), "health" );
    return power > 0 ? TRACE_Record : TRACE_Stop;
}

script "PierceShot" ( void )
{
    int t = fua_TracerCreate();
    fua_TraceRule( t, TR_ACTOR, TRACE_Callback );
    fua_TraceRule( t, TR_WALL, TRACE_Stop );
    fua_TraceCallback( t, Pierce );
    power = 100;
    int n = fua_Trace( t, x, y, z, dx, dy, dz, 4096.0, 0, fua_GetActorHandle( 0 ) );
}
```

- **Rules come first.** Only the hits a `TRACE_Callback` rule hands over call the function, one ACS
  function call each, so cheap cases stay with the rules.
- **The function runs inside the trace,** as the calling script's activator, with its own module's
  variables. `ctx` carries the caller's state, such as an index or a static struct, since functions
  have no closures.
- **A callback that waits** (only other compilers allow `Delay` in a function), or returns anything
  but an action from `TRACE_Stop` to `TRACE_Record`, stops the trace on that hit; a wait also warns
  once a map.
- **A callback can't start a trace;** `fua_Trace` and `fua_LineTrace` return 0 inside one.
- **An actor a callback removes** is passed by if the trace reaches it.
- Many decisions need no callback: record every hit with a rule and walk the list after the trace.

## Implementation notes

[`src/zandronum/src/features/acs-traces/`](../../../src/zandronum/src/features/acs-traces/README.md).

# Finding actors in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX; elsewhere they return 0. Results are
> [actor handles](Actor_handles.md).

| Number | Function | Returns |
|---|---|---|
| 24040 | `fua_QueryCreate()` | a query id, or 0 when the script already holds 16 |
| 24041 | `fua_QueryFree(int q)` | nothing; also happens when the script ends |
| 24042 | `fua_QueryClear(int q [, int kind])` | nothing; drops every filter and the sort, or only filters of one kind |
| 24043 | `fua_QueryFilter(int q, int kind [, a [, b]])` | 1 once added; 0 for a bad call, an unknown class or flag, or a handle to no actor |
| 24044 | `fua_QuerySort(int q, int mode [, int max])` | nothing; `FQ_NEAREST`, `FQ_FARTHEST` or `FQ_NONE`, and a result cap |
| 24045 | `fua_FindActors(int q, fixed x, fixed y, fixed z, fixed radius)` | how many actors in range passed the filters |
| 24046 | `fua_FindActorsOfClass(int q, str cls [, bool subclasses [, fixed x, fixed y, fixed z]])` | how many actors of the class passed the filters |
| 24047 | `fua_FoundActor(int q, int i)` | the i-th result's handle, 0 once that actor is gone |
| 24048 | `fua_FoundCount(int q)` | how many results the query holds |

## Usage

```
int q = fua_QueryCreate();
fua_QueryFilter( q, FQ_MONSTER );
fua_QueryFilter( q, FQ_ALIVE );
fua_QueryFilter( q, FQ_SIGHT, fua_GetActorHandle( 0 ) );
fua_QuerySort( q, FQ_NEAREST, 3 );
int n = fua_FindActors( q, GetActorX( 0 ), GetActorY( 0 ), GetActorZ( 0 ), 512.0 );
for( int i = 0; i < n; i++ )
{
    fua_SetActivatorHandle( fua_FoundActor( q, i ) );
    Thing_Damage2( 0, 10, "Fire" );
}
```

| Filter | Arguments | Keeps actors that |
|---|---|---|
| `FQ_CLASS` | class name, include subclasses (default 1) | are of that class |
| `FQ_NOTCLASS` | class name, include subclasses (default 1) | are not of that class |
| `FQ_ALIVE` / `FQ_DEAD` | none | have health above 0 / at most 0 |
| `FQ_MONSTER` / `FQ_PLAYER` / `FQ_MISSILE` | none | are monsters / player pawns / projectiles |
| `FQ_SHOOTABLE` | none | have `SHOOTABLE` |
| `FQ_FLAG` | flag name, set (default 1) | have that DECORATE flag set, or clear with 0 |
| `FQ_TID` | tid | have that TID |
| `FQ_FRIENDLY` / `FQ_HOSTILE` | handle | are friends / enemies of that actor (players count as friendly) |
| `FQ_SIGHT` | handle | that actor can see |
| `FQ_EXCLUDE` | handle | are not that actor |
| `FQ_2D` | none | are in range ignoring height |
| `FQ_EDGES` | none | touch the range with their radius and height, not only their origin |

- **Every filter must match.** Filters stay until `fua_QueryClear`, so a loop can run the same query every
  tic. Cheap checks run first and line of sight last, whatever order they were added in.
- **A handle filter whose actor is gone matches nothing,** so a dead viewer's `FQ_SIGHT` keeps no one.
  An unknown class or flag matches nothing either, except `FQ_NOTCLASS`, which then keeps everyone.
- **Results** are kept as handles until the query runs again or is freed, and read at the script's own
  pace, across `Delay`s too. Their order is the blockmap's (the map's actor list for
  `fua_FindActorsOfClass`), or distance from the centre with ties in that order: the same world gives
  the same list. At most 1024, or the cap `fua_QuerySort` set.
- **`fua_FindActors` only finds actors in the blockmap,** measured from their origin. `NOBLOCKMAP` actors
  (most puffs and many effects) need `fua_FindActorsOfClass`, which walks every actor of the class on
  the map, carried inventory left out, and costs more. Its `x`, `y`, `z` are only for the sort.
- **A query belongs to its script.** Each script holds up to 16; another script's id, or a freed one,
  finds nothing. Queries are saved with their script, results included, so a save in the middle of a
  loop resumes it.
- **Each machine queries its own world;** nothing is sent.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

The nearest live imp, by asking for one result sorted by distance:

```bcs
#library "fuanear"
#import "zcommon.bcs"

script "NearestImp" ( void ) {
   int q = fua_QueryCreate();
   fua_QueryFilter( q, FQ_CLASS, "DoomImp" );
   fua_QueryFilter( q, FQ_ALIVE );
   fua_QuerySort( q, FQ_NEAREST, 1 );
   if ( fua_FindActors( q, GetActorX( 0 ), GetActorY( 0 ), GetActorZ( 0 ), 2048.0 ) ) {
      Log( s: "nearest imp ", d: fua_GetField( fua_FoundActor( q, 0 ), "tid" ) );
   }
   else {
      Log( s: "no imp near" );
   }
   fua_QueryFree( q );
}
```

<!-- example
pukename NearestImp => /nearest imp 31/
pukename Place 900 680 128
pukename NearestImp => /nearest imp 33/
-->

A blast that hurts every live monster the player can see within 512 units:

```bcs
#library "fuablast"
#import "zcommon.bcs"

script "Blast" ( void ) {
   int me = fua_GetActorHandle( 0 );
   int q = fua_QueryCreate();
   fua_QueryFilter( q, FQ_MONSTER );
   fua_QueryFilter( q, FQ_ALIVE );
   fua_QueryFilter( q, FQ_SIGHT, me );
   int n = fua_FindActors( q, GetActorX( 0 ), GetActorY( 0 ), GetActorZ( 0 ), 512.0 );
   for ( int i = 0; i < n; ++i ) {
      if ( fua_SetActivatorHandle( fua_FoundActor( q, i ) ) ) {
         Thing_Damage2( 0, 30, "Fire" );
      }
   }
   fua_SetActivatorHandle( me );
   fua_QueryFree( q );
   Log( s: "blasted ", d: n );
}
```

<!-- example
pukename Blast => /blasted 0/
pukename Place 400 680 0
pukename Blast => /blasted 3/
pukename Health => /drv imps 30 30 30/
-->

## Best practices

- **One query, reused.** Filters stay until `fua_QueryClear`, so a script that checks every tic creates
  its query once, before the loop, and runs it each tic; `fua_QueryClear( q, kind )` drops only the
  filters of one kind, such as an `FQ_EXCLUDE` that changes.
- **Free what you create,** or let the script end: a script holds at most 16 queries.
- **Narrow with filters, not loops.** `FQ_ALIVE`, `FQ_CLASS` and the rest run in the engine, cheap ones
  first and line of sight last, for much less than a script loop testing each result.
- **Cap the result.** `fua_QuerySort( q, FQ_NEAREST, n )` keeps only the `n` nearest, which is all a
  targeting script needs.
- **Prefer `fua_FindActors`** over `fua_FindActorsOfClass`: it reads the blockmap around the centre,
  where the class search walks every actor of that class on the map.
- **Results are handles.** An actor a result names can go between the query and the loop's use of it;
  functions given its handle then do nothing.

## Online

A server script finds actors in the server's world; a clientside script finds them in its client's, which
the server keeps in step, so the same query gives the same actors once updates have arrived. `FQ_PLAYER`
finds every player in the game. Damage and kills belong in server scripts: a client's damage only reaches
actors that exist on that client alone.

## Implementation notes

[`src/zandronum/src/features/acs-queries/`](../../../src/zandronum/src/features/acs-queries/README.md).

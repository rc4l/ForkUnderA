# Bouncing actors

Bouncing projectiles could hang in the air or bounce on past their `BounceCount`, and a client's copy of
a server's bouncer could freeze in front of a monster for good. Each case had a path that neither
bounced the actor nor ended it:

- **A spent count off an actor.** `P_BounceActor` returned false on the last bounce. Its callers on top
  of an actor and for MBF bouncers ignored that, and the spent count of 0 then meant unlimited. It now
  ends the bouncer itself (uzdoom@1eb2c7532, uzdoom@46ee2c5ec).
- **On top of an actor it does not bounce off.** It kept its vertical speed with no gravity and hung
  there. A missile now explodes on it (uzdoom@5ca1bb397); anything else lands on it.
- **A plane it does not bounce off.** A walls-only bouncer stopped by a ramp "bounced" off the floor
  without changing anything and pressed into it forever. It now explodes as any missile would
  (uzdoom@72ed0c437).
- **The wrong line.** `PIT_CheckLine` overwrites `BlockingLine` with every later line that narrows the
  box, so a wall that refused the move lost it to a two-sided divider beside it. At a corner the box
  also crosses the face it slides along. When `P_BounceWall`'s corner trace misses the wall, which it
  does for anything faster than 16 a tic, it reflected off that line instead, often straight back into
  the wall, and its push-out then shoved the projectile through it. `FCheckPosition::refusedline` keeps
  the refusing line the move runs into most directly (`ComputeHeadOn`), and a failed move names it first.
- **The server's word on an actor bounce.** The position and velocity after a bounce off an actor were
  sent from the end of `P_ZMovement`. Its floor and ceiling bounces return before that point, and a
  bouncer on top of an actor never calls it, so a client could go without the update indefinitely. It
  is now sent from `AActor::Tick`.
- **A client's own actor collisions.** A client stopped a server-owned bouncing missile at any actor
  in its way and waited for the server, which only spoke up if its own copy hit something. When the
  client saw a monster where the server's was not, its copy hung there. The server sends every actor
  bounce and every explosion with the position, so a client now leaves actor collisions to it, as it
  already does for non-bouncing missiles. Walls are still clipped on the client.

`computation/bounce_compute.*` holds the rules; `tools/fuactl/scenarios/bounce.mjs` (single player)
and `netbounce.mjs` (a hosted game) are the regression tests.

## In-engine hooks

- `p_local.h`: `FCheckPosition::refusedline`.
- `p_map.cpp`: `PIT_CheckLine` (the refusing line), `P_CheckPosition` and `P_TestMobjZ` (the thing walk a
  client skips), `P_TryMove`'s `pushline` (`ComputeBlockedLine`), `FSlide::BounceWall`, and
  `P_BounceActor`.
- `p_mobj.cpp`: `AActor::FloorBounceMissile`, the on-top bounce in `AActor::Tick`, and
  `P_SendBounceOffActor`, called from `AActor::Tick`.

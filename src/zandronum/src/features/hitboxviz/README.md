# hitboxviz

Client-side debug overlay drawing collision volumes in the 3D view:

- **Collision box** (blue) — `radius` / `height`, the volume the blockmap tests.
- **Hurtbox / attack box** (green) — `GetAttackRadius()` / `GetAttackHeight()` (i.e.
  `HitRadius` / `HitHeight`), the extent at which the actor can actually be hit. Drawn **only
  when it differs** from the collision box, so ordinary actors aren't outlined twice.
- **Explosion regions** (red) — the area `P_RadiusAttack` actually tested, plus the inner
  full-damage region and a cross marking the blast's own height.

Everything is read fresh each frame, so `A_SetSize`, `A_SetHitSize`, `APROP_Radius`,
`APROP_HitRadius` and crouch shrink are all reflected on the next frame. Nothing is
snapshotted (see *Provenance* — the reference implementation gets this wrong).

GL and Vulkan; the software renderer has no world-space line path.

## Toggles

Options → **FUA Options** → **Hitbox Visualization**, plus a bindable `fua_hitbox` command
under Customize Controls → FUA.

| CVAR | Default | |
|---|---|---|
| `cl_fua_hitbox` | off | master toggle; the rest gray out behind it. A dev-mode setting: a client of a server without `sv_cheats` is refused it |
| `cl_fua_hitbox_actors` / `_missiles` | on | which actors get boxes |
| `cl_fua_hitbox_onetic` | on | draw actors spawned and destroyed inside one tic, such as a weapon's one-tic hitbox (see below) |
| `cl_fua_hitbox_attackbox` | on | draw the attack box when it differs |
| `cl_fua_hitbox_explosions` | on | draw explosion regions |
| `cl_fua_hitbox_self` | on | include the camera's actor — your own body, or the player you are spectating |
| `cl_fua_hitbox_xray` | off | ignore the depth buffer — see boxes through walls |
| `cl_fua_hitbox_linewidth` | 2 | clamped to `GL_ALIASED_LINE_WIDTH_RANGE` |
| `cl_fua_hitbox_color` / `_attackcolor` / `_blastcolor` | blue / green / red | collision box / hurtbox / explosion |
| `sv_debugexplosions` | off | **server**: broadcast explosion regions (`CVAR_SERVERINFO`). Works only while `sv_cheats` is also true. Off by default because every explosion then goes as a reliable command to every client, whether it draws boxes or not: about 20 kB/s on a crowded map. |

`cl_fua_hitbox_self` exists because spectating attaches the camera to the followed player's
body, so skipping the camera actor (which the sprite path does, to avoid drawing a wireframe
you are standing inside) also hid the box of whoever a spectator was watching.

### Cheat gating

`cl_fua_hitbox` and `fua_hitbox` are dev-mode tools (`features/dev-mode`). Offline, on a server, or
as a client of a server with `sv_cheats` on, they work as any setting does. A client of a server
without cheats is refused them, and a value set before it joined is held off and given back once
it may have it again. That matters because `cl_fua_hitbox_xray` is a wallhack by construction;
every other option draws nothing while the toggle is off.

The toggle stays `CVAR_ARCHIVE`, unlike `am_cheat` ("this is a cheat so don't save it"), since the
gate saves the user's own value rather than the one it holds.

⚠️ `sv_cheats` is `CVAR_LATCH`: on a server, a mid-game `sv_cheats 1` is queued and **only takes
effect on the next map**, and the client is refused until then.

The **server-side** switches are unaffected: `sv_debugexplosions` and the debug-only hit-size
replication still require `sv_cheats` (`ServerDebugActive()`), because those are about what a
server puts on the wire, not about what a local machine draws for itself. Offline never
reaches them — `P_RadiusAttack` records the region directly.

## Explosions are events, not objects

`P_RadiusAttack` runs and returns inside a single tic, so there is no actor to hang the
visualization on — regions go into a bounded store (`computation/blastrecords_compute.h`, the
shared `recordstore_compute.h`) and are drawn for ~1 second.

`A_Explode` and `A_RadiusThrust` return early in client mode
(`thingdef_codeptr.cpp:1113`/`1180`), so a client never runs `P_RadiusAttack` at all. That is
why this is networked: `SVC2_DEBUGEXPLOSION` carries the **server's authoritative** region
rather than a client-side guess. Sent only while `sv_debugexplosions` **and** `sv_cheats` are
on, so production servers pay nothing.

The command is built purely from Zandronum's own networking: the `protocolspec` generator, a
`NetCommand` in the `SVC2` extended-command space, and `sendCommandToClients`. Nothing is
borrowed from Q-Zandronum's transport — its debug hitboxes are *networked spawned actors*
(`SERVERCOMMANDS_SpawnThing` of a `DebugUnlaggedHitbox`), which was deliberately not used.

**Debugging a silent feed:** run `cl_showcommands 1` on the client. Each received
`SVC2_DEBUGEXPLOSION` prints by name, which separates "the server never sent it" from "it
arrived but nothing was drawn".

**The damage region is a square prism, not a sphere.** `p_map.cpp` computes
`len = max(dx, dy)` under the comment *"The damage pattern is square, not circular."* The
overlay draws that square region; a circle would report actors in the corners as safe when
they are not. Two things the wireframe deliberately cannot show: the `P_CheckSight`
requirement, and that the effective radius is per-target (each target's own attack radius is
subtracted from the distance).

## One-tic actors are events too

A weapon acts in `P_PlayerThink`, before `DThinker::RunThinkers`, and a fresh thinker ticks in the
tic it was spawned (`TickThinkers`). So an actor a weapon spawns with a one-tic spawn state (MM8BDM
classes use them as hitboxes, such as Toad Man's leap crush, spawned under him each tic of the
fall) is destroyed inside the tic that made it. Every copy of the game is taken between tics, so no
copy ever holds it and no box was drawn. Frozen (`freeze`) such actors never tick and pile up, which
is why they showed only then.

`AActor::fua_spawnTic` is the `gametic` an actor was spawned in. `AActor::Destroy` offers each
actor to `NoteDestroyed`, which keeps the boxes of one spawned in that same tic
(`ComputeTicBoxWanted`): a missile, solid or shootable, never `+NOINTERACTION`, so the zero-tic
helpers mods spawn by the dozen stay out. The record holds where its tic began (`PrevX`) and where
it went, reaches the frames through the handoff as a blast does, and is drawn by the frames of the
copy of that tic's end, whose level time is one more (`P_Ticker`'s `level.time++`,
`ComputeTicBoxShown`), interpolated like a live actor, under the same actor/missile and attack-box
toggles. The level time and not `gametic` is the clock: `gametic` goes on through a pause, where the
copy still shows the last tic that ran, and those boxes with it. An actor a script spawns
after its thinkers have run lives to the tic's end and is in that copy, so the record leaves it out.

## Attack-extent replication is debug-only

`AActor::SetHitSize` is normally unnetworked — the attack extent is server-authoritative.
That would leave a netgame client drawing the DECORATE default and silently missing runtime
changes, so `SVC2_SETTHINGSIZE` gained two optional fields (`ACTORSIZE_HITRADIUS` /
`ACTORSIZE_HITHEIGHT`, `network.h`) that are populated **only** when
`zx::hitboxviz::ServerDebugActive()`. Purely additive: ordinary senders never set those bits,
so existing traffic is byte-for-byte unchanged. See also
[`features/actorresize/README.md`](../actorresize/README.md).

## Layout

`computation/` is engine-free and unit-tested (`tests/coverage.sh --auto`):

- `boxedges_compute` — box edges as a `GL_LINES` vertex list, the blast prism, and the
  `fulldamagedistance` clamp mirrored from `P_RadiusAttack`.
- `recordstore_compute` — the bounded, insertion-ordered store of expiring events.
- `blastrecords_compute` — a blast's record and the blasts' store.
- `ticboxes_compute` — a one-tic actor's record, which going actors are kept and which frames show them.
- `vizgate_compute` — `ResolveLineWidth`.

`hitboxviz.{h,cpp}` is the engine glue every build has:
- the cvars and the `fua_hitbox` CCMD;
- the boxes, the one-tic actors' records and the blasts;
- the backend's collection (`CollectBackendFrame`, `BackendLines`);
- the server's side.

`ticboxgame.cpp` is the one-tic actors' game half: `NoteDestroyed` reads the live actor a tic destroys
and hands a plain record to `PushTicBox`, so the renderer's files never read the game
(`tools/boundary-rules.json` excludes it from `renderer-reads-the-copy`, as it does the damage tint's).

`hitboxviz_gl.cpp` is GL's half: per-scene collection and the GL draw. Only builds with GL
compile it, so a build without GL (the server, the browser) still has the backend's overlay.
`hitboxviz_internal.h` is what the two files share.

### Scene nesting

`DrawScene` runs `CreateScene`, then `GLPortal::EndFrame` — which **re-enters `DrawScene`**
once per visible portal (`gl_portal.cpp:696`/`790`/`845`/`957`) — and only then
`RenderTranslucent`. The outer view has therefore finished collecting before an inner view
starts, so a single flat buffer would draw the outer view's boxes inside the portal and leave
nothing for the outer view. Each scene instead marks where its own geometry begins and
truncates back to that mark once drawn. The mark is popped on every exit path, including the
"nothing to draw" one.

### Core profile

`glBegin` is unusable here (this engine's GL path is core-profile safe), so geometry streams
through the flat VBO like `FGLRenderer::DrawLine`. `FFlatVertexBuffer::GetBuffer()` is
unchecked and the buffer keeps only 500 vertices of slack past its wrap threshold, so batches
are chunked to 480 (= 20 whole boxes, and even, so no chunk ever splits an edge).

Wide lines are optional in a core profile — `glLineWidth` above 1.0 may raise
`GL_INVALID_VALUE` and be ignored — so the width is clamped to the driver's reported range.
On a core profile reporting `[1, 1]` the slider is inert; camera-facing quads would fix that
and are not implemented.

## In-engine hooks (edits to existing files, not part of this folder)

- `gl/scene/gl_bsp.cpp` — `RenderThings` collects each actor. Chosen over `GLSprite::Process`
  because that path culls spriteless, `+INVISIBLE` and fully translucent actors, which still
  have collision boxes worth seeing.
- `gl/scene/gl_scene.cpp` — `BeginFrame()` in `CreateScene`, `Draw()` at the tail of
  `RenderTranslucent` (3D projection current, depth buffer writable again).
- `features/frame/standaloneframe.cpp` — `CollectBackendFrame` with the player's walk's sectors,
  for the Vulkan backend, whose `features/hwrender/diligent/dglines.cpp` draws `BackendLines`
  after the mirrors and before the 2D. It draws the player's view only (GL also draws boxes
  inside portals), and each edge is a screen-space quad `cl_fua_hitbox_linewidth` pixels wide
  (`wideline_compute`), so the width is not clamped to a driver's line range.
- `p_map.cpp` — record/broadcast in `P_RadiusAttack` (right after the `fulldamagedistance`
  clamp, so the drawing can't disagree with the simulation); debug-only send in
  `AActor::SetHitSize`.
- `p_setup.cpp` — `ClearRecords()` in `P_SetupLevel`, so regions and one-tic boxes don't survive a map change.
- `actor.h` — `fua_spawnTic`, among the cold fields; `p_mobj.cpp` — `AActor::StaticSpawn` sets it,
  `AActor::Destroy` calls `NoteDestroyed` first, while the actor still stands where it went.
- `network.h`, `network_enums.h`, `sv_commands.{h,cpp}`, `cl_main.cpp`,
  `protocolspec/spec.misc.txt`, `protocolspec/spec.things.txt` — `SVC2_DEBUGEXPLOSION` and the
  two optional hit-size fields.
- `wadsrc/static/menudef.txt` — the submenu, the FUA Options row, the Customize Controls bind.
- `CMakeLists.txt` — sources listed **before `zzautozend.cpp`** (see `features/README.md`).

## Provenance

Adapted from Q-Zandronum's `gl_show_hitbox` / `hitboxColor`
([`9be79f2`](https://github.com/Qbical/Q-Zandronum/commit/9be79f20c25b4bff053a9f5288a601b1659884c6),
extended in `35ee480`), `src/gl/scene/gl_sprite.cpp`. The *idea* is theirs; the code is not
reusable — it is immediate mode against a GZDoom-1.8-era renderer. Three defects were
deliberately not carried over:

1. `glColor3f` fed `PalEntry`'s 0-255 bytes, so every non-zero channel clamps to saturated and
   `HitboxColor` collapses to eight possible colours.
2. Box coordinates taken un-interpolated while the sprite uses `r_TicFrac`, so boxes visibly
   lag their own sprites above 35fps.
3. One `glBegin`/`glEnd` pair per edge — 14 draw calls per actor.

Their unlagged debug actor also copies `radius`/`height` off its target once in
`PostBeginPlay` (`unlagged.cpp:73-77`); that snapshot goes stale the moment anything is
resized, which is exactly what this feature exists to inspect.

UZDoom has nothing to port: no `hitbox` anywhere in the tree, no world-space line primitive
(its only `DT_Lines` consumer is the 2D HUD drawer), and no explosion visualization. Its sole
collision visual is a 2D automap square at `t->radius` behind `am_cheat 3`
(`am_map.cpp:3158`), whose `netgame && !sv_cheats` cvar clamp this engine already mirrors at
`am_map.cpp:633`.

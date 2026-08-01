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

GL renderer only; the software renderer has no world-space line path.

## Toggles

Options → **FUA Options** → **Hitbox Visualization**, plus a bindable `fua_hitbox` command
under Customize Controls → FUA.

| CVAR | Default | |
|---|---|---|
| `cl_fua_hitbox` | off | master toggle; the rest gray out behind it. Offline it is the only thing needed; online it also takes `sv_cheats` |
| `cl_fua_hitbox_actors` / `_missiles` | on | which actors get boxes |
| `cl_fua_hitbox_attackbox` | on | draw the attack box when it differs |
| `cl_fua_hitbox_explosions` | on | draw explosion regions |
| `cl_fua_hitbox_self` | on | include the camera's actor — your own body, or the player you are spectating |
| `cl_fua_hitbox_xray` | off | ignore the depth buffer — see boxes through walls |
| `cl_fua_hitbox_linewidth` | 2 | clamped to `GL_ALIASED_LINE_WIDTH_RANGE` |
| `cl_fua_hitbox_color` / `_attackcolor` / `_blastcolor` | blue / green / red | collision box / hurtbox / explosion |
| `sv_debugexplosions` | **on** | **server**: broadcast explosion regions (`CVAR_SERVERINFO`). Bandwidth escape hatch only — it does nothing unless `sv_cheats` is also true, so it is not a second enable switch. |

`cl_fua_hitbox_self` exists because spectating attaches the camera to the followed player's
body, so skipping the camera actor (which the sprite path does, to avoid drawing a wireframe
you are standing inside) also hid the box of whoever a spectator was watching.

### Cheat gating

| where | draws when |
|---|---|
| **Offline single player** | `cl_fua_hitbox` is on. `sv_cheats` is not consulted. |
| **Client of a server, or a server** | `cl_fua_hitbox` is on **and** `sv_cheats` is true. |

"Offline" is the engine's own definition — neither a client nor a server
(`NETWORK_InClientMode()` false and state not `NETSTATE_SERVER`), which includes
`NETSTATE_SINGLE_MULTIPLAYER`, an offline game merely emulating multiplayer with bots. It is
the same condition that lets `iddqd` work offline with `sv_cheats` off: nobody else is in the
game and there is no server whose rules could be subverted, so `sv_cheats` is not the
authority there. Requiring it anyway made this the one debug view unusable in the exact
situation it is most useful — a local test map, where `sv_cheats` is latched and so needs a
map change before it applies.

The moment there *is* someone to protect, `sv_cheats` is the sole authority. That matters
because `cl_fua_hitbox_xray` is a wallhack by construction; keep it behind this gate.

The gate (`ShouldDraw` in `computation/vizgate_compute.h`) is spelled out from its two inputs
rather than delegated to `CheckCheatmode()`, which applies the same offline rule but *also*
refuses on `DisableCheats` skills — a skill definition should not be able to switch off a
debug renderer.

The check runs every frame rather than resetting the cvar from a callback the way
Q-Zandronum's `gl_show_hitbox` does, so the settings survive joining a cheats-disabled server
and simply stop drawing. That is also why they are `CVAR_ARCHIVE`, unlike `am_cheat` ("this
is a cheat so don't save it"): they are a view preference that is inert where cheats are
refused, not cheat state.

⚠️ `sv_cheats` is `CVAR_LATCH`: on a server or as a client, a mid-game `sv_cheats 1` is
queued and **only takes effect on the next map**. Set it before loading, or change map after
setting it. The `fua_hitbox` CCMD says so when you enable the overlay online with cheats off
— offline it just confirms the toggle, since the latch is irrelevant there.

The **server-side** switches are unaffected: `sv_debugexplosions` and the debug-only hit-size
replication still require `sv_cheats` (`ServerDebugActive()`), because those are about what a
server puts on the wire, not about what a local machine draws for itself. Offline never
reaches them — `P_RadiusAttack` records the region directly.

## Explosions are events, not objects

`P_RadiusAttack` runs and returns inside a single tic, so there is no actor to hang the
visualization on — regions go into a bounded store (`computation/blastrecords_compute.h`)
and are drawn for ~1 second.

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
- `blastrecords_compute` — the bounded, insertion-ordered blast store.
- `vizgate_compute` — `ShouldDraw` (the full offline / `sv_cheats` truth table) and
  `ResolveLineWidth`.

`hitboxviz.{h,cpp}` is the engine glue: cvars, the `fua_hitbox` CCMD, per-scene collection,
and the GL draw.

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
- `p_map.cpp` — record/broadcast in `P_RadiusAttack` (right after the `fulldamagedistance`
  clamp, so the drawing can't disagree with the simulation); debug-only send in
  `AActor::SetHitSize`.
- `p_setup.cpp` — `ClearBlasts()` in `P_SetupLevel`, so regions don't survive a map change.
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

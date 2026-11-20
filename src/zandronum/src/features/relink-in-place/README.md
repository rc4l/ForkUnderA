# Relinks whose outcome is already known

On Sunder MAP11 with Complex Doom, `AActor::Tick` relinked ~5,100 `NOINTERACTION` actors a tic (trails, smoke),
4,000 of them not moving in x or y, and built ~12,500 sector lists a tic, 3,000 of them for radius-0 actors.

- **In place.** A `NOINTERACTION` tick unlinks and links its actor again. When the actor is where its last link put
  it (same x, y, radius and `NOSECTOR`, that link's sector found from the point, not in the blockmap) on a map with
  no polyobjects, whose walls are the only ones that move, the relink would find the same sector, subsector and
  sector list, and only move the actor to the head of its sector's things. `AActor::RelinkInPlace` does just that,
  with the sector and subsector the last link set, and uses up the sector list's replay recording as a build would.
- **Radius 0.** `P_CreateSecNodeList` walks the lines near the actor for those its box straddles; a radius-0 box has
  one corner, so none can (`FBoundingBox::BoxOnLineSide` compares a corner with itself), and the walk is skipped.
  `P_CheckPosition` skips its line walk for the same reason: `PIT_CheckLine` passes every line such a box meets.
  Complex Doom's sparks and trails are radius-0 missiles, ~4,400 moves a tic on Sunder MAP11; each move got
  ~12-15% cheaper, and the CD and vanilla lap traces agree tic for tic.
- **Client-side effects.** A `NOINTERACTION` actor with `CLIENTSIDEONLY` never exists on a server, and nothing
  that walks a sector's touching things acts on it but by opt-in: moving floors skip it unless it has
  `MOVEWITHSECTOR`, wind needs `WINDTHRUST`, floor clipping `FLOORCLIP`. So it builds no sector list, unless it
  has one of those three flags. It stays in its sector's things, which the renderer draws from. Also left out:
  a sector-tag group teleport no longer carries it, and it no longer keeps an animated (Strife) door from closing.

`computation/relink_compute.*`: the rules. Hooks: `actor.h` (`FuaLinkedAt`, `RelinkInPlace`), `p_maputl.cpp`
(`LinkToWorld` records the link and builds or frees the sector list, `RelinkInPlace`), `p_mobj.cpp` (`AActor::Tick`'s `NOINTERACTION` branch),
`p_map.cpp` (`P_CheckPosition`, `P_CreateSecNodeList`, `P_DiscardSecnodeReplay`).

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

`computation/relink_compute.*`: both rules. Hooks: `actor.h` (`FuaLinkedAt`, `RelinkInPlace`), `p_maputl.cpp`
(`LinkToWorld` records the link, `RelinkInPlace`), `p_mobj.cpp` (`AActor::Tick`'s `NOINTERACTION` branch),
`p_map.cpp` (`P_CreateSecNodeList`, `P_DiscardSecnodeReplay`).

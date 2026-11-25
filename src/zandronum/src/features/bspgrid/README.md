# features/bspgrid

Where a point's walk down a BSP tree to its subsector can start.

`P_PointInSubsector` and `R_PointInSubsector` walk from the root, one dependent node load per level. On a large map
that is 16-30 loads, for every actor that moves, every position check and every sound. In slaughter-map fights
those walks were 3.6-5.6% of the game thread.

- `computation/bspgrid_compute`: a grid over the map. Each cell holds the deepest node that every point in it
  reaches the same way, or the subsector itself when the whole cell lies in one. A walk from there lands on the same
  subsector as a walk from the root.
  - The side test is `R_PointOnSide`'s, a threshold on a function linear in x and y. So when a box's four corners
    agree, every point in it does.
  - Corners are never trusted where the arithmetic could leave exact integers: at a node whose fast-path product
    sum can overflow (`dx` and `dy` both `INT32_MIN`), or past 2^46.
  - The grid is built top down: a box walks down while its corners agree, then splits in four.
  - Cells are 64 map units at the finest, at most a million per tree.
  - A test holds every lookup to a walk from the root, across 60 random trees and their partitions, cell edges and
    points outside the map.
- `computation/bspgrid_compute`: each tree's nodes copied in preorder (40 bytes a node, against `node_t`'s 120, with
  integer children), and the grid's starts renumbered to match. The few levels a walk takes from a grid start then sit
  together in memory, mostly in one or two cache lines.
- `bspgrid.{h,cpp}`: the level's two grids and compact copies, the game's tree and the renderer's (one when they are
  the same).
  - The walk uses `R_PointOnSide`'s own test (`R_PointOnPartition` in `r_utility.h`) on the copied fields.
  - Each grid remembers the tree and node count it was built from, and a lookup checks both, so a tree replaced
    since walks from its root as before.

## In-engine hooks

- `p_setup.cpp` `P_SetupLevel`: `BuildForLevel` once both trees are final, after the GL nodes. `P_FreeLevelData`
  calls `ForgetLevel` before the nodes go.
- `p_glnodes.cpp` `P_PointInSubsector`: answers from `GameSubsector`.
- `r_utility.cpp` `R_PointInSubsector`: answers from `RenderSubsector`.
- `r_utility.h`: `R_PointOnSide`'s test as `R_PointOnPartition`, which both walks share.
- `src/CMakeLists.txt`: `bspgrid.cpp` in the engine's sources.

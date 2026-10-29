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
- `bspgrid.{h,cpp}`: the level's two grids, the game's tree and the renderer's (one grid when they are the same).
  - Each grid remembers the tree and node count it was built from, and a lookup checks both, so a tree replaced
    since walks from its root as before.
  - `fua_bspgrid_check 1`: at each level's load, every cell's corners and four points inside are walked from the
    cell's start and from the root, and the differences printed. It should read 0.

## In-engine hooks

- `p_setup.cpp` `P_SetupLevel`: `BuildForLevel` once both trees are final, after the GL nodes. `P_FreeLevelData`
  calls `ForgetLevel` before the nodes go.
- `p_glnodes.cpp` `P_PointInSubsector`: starts at `GameStart`.
- `r_utility.cpp` `R_PointInSubsector`: starts at `RenderStart`.
- `src/CMakeLists.txt`: `bspgrid.cpp` in the engine's sources.

# Borrowing from UZDoom: what applies, what does not, and why

Checked against the tree at `/f/UZDoom` (`fbad53bff5`), not from memory.

## The one structural difference that decides everything

Upstream derives the scene **every frame**. `HWDrawInfo::RenderBSP` walks the BSP and constructs
`HWWall wall;` per seg and `HWFlat flat;` per subsector, then calls `wall.Process(...)` -- in their
Vulkan backend exactly as in their GL one. What they moved behind an abstraction is DRAWING, through
`FRenderState`; derivation never moved, because it is not an API concern.

Ours is resident. The mesh is baked from the map and persists, which is why GL's walk can be switched
off at all, and it is the whole of the difference measured today: **11.3 ms to 1.1-1.3 ms** on Sunder
MAP10's arena.

So the rule for borrowing is: **take their logic, never their loop.** Anything that reads
`HWDrawInfo` and produces a frame's worth of surfaces is the thing we just spent a day removing.

## What ports cleanly

**Sprite logic.** `hw_sprites.cpp` has **zero** GL references in 1822 lines. `HWSprite::Process` is
API-agnostic as written, and so is ours: `GLSprite::Process` touches GL state **0 times** against
`GLSprite::Draw`'s 27. The hard part of sprites -- clipping against floor and ceiling, mirrored frame
selection, wall versus face orientation, `SplitSprite`'s light bands, the render styles -- needs no
porting at all, in either tree. Only the emission half is API-bound.

Acted on: GL no longer rasterises sprites when the backend is carrying the frame. Registration into
the mesh happens before the draw, so nothing changes about what is rendered -- only that 1380 draw
calls a frame stop being issued into a framebuffer that is never presented. **1.47 ms to 1.01 ms** at
a fixed vantage with 1074 sprites, and 0.6% parity, unchanged.

## What does not port, and would not help if it did

**Portals.** Ours is 1127 lines with 71 GL-state uses -- stencil, clears, render-to-texture recursion.
Theirs is cleaner (1205 lines, 25 `HWDrawInfo`/`FRenderState` references) but it is recursive by
nature: a portal renders the scene again from another viewpoint. There is no resident form of that,
so borrowing buys a better-factored version of work that has to happen per frame regardless. Portals
are a feature to implement in the backend, not a thing to inherit.

**The 2D drawer.** `F2DDrawer` is API-agnostic and would replace our capture of GL's 2D path -- but it
arrives with the material model behind it, which is the 44k-LOC port `docs/texture-system-decision.md`
already declined for the same reason.

**`FRenderState` itself.** Adopting it wholesale is the tempting move and the wrong one: it is the
interface their per-frame renderer draws through. Our backend would sit under it and inherit the
architecture it was built to serve.

## The honest summary

Borrowing does not "get us everything". It gets us sprites -- which we already effectively had, and
which is now off GL's rasteriser -- and it tells us the remaining gaps are features to build rather
than code to copy. Upstream is far ahead on breadth: every render style, every portal kind, models,
voxels, three backends, years of them. We are ahead on one axis only, and only while the scope stays
narrow enough that resident geometry is possible.

The version of "better than them" that survives contact with the measurements: **their logic, our
residency.** Take `Process`-shaped code, which is portable in both trees and always was. Leave
`HWDrawInfo`-shaped code, which is the per-frame cost we removed.

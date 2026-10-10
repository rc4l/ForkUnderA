# features/glportal

GL portal-ordering fixes, with the decisions kept in `computation/glportal_compute` so they are tested.

`GLPortal::RenderFirstSkyPortal` draws one sky-like portal before the scene, and skips any that need the
depth buffer once it is inside a portal recursion. It read that recursion from `DrawScene`'s own counter, which
only rises around `EndFrame`, so a stacked view drawn first re-entered `DrawScene` at recursion 0 and could draw
another stacked view first, without end. A stacked room wider than the room under it did this at load and the
engine died of a stack overflow. `ComputeDrawsSkyPortalFirst` also takes the portal depth, which every nested
`CreateScene` raises.

## In-engine hooks

- `gl/scene/gl_portal.cpp` `GLPortal::RenderFirstSkyPortal`: asks `ComputeDrawsSkyPortalFirst` with `renderdepth`.

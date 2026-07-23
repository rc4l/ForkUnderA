# Vendored: UZDoom `src/common/rendering/`

This tree is a **verbatim vendor** of UZDoom's backend-agnostic renderer half. Keep the upstream
layout so future pulls are a directory-scoped diff, not a fork rebase (see
`docs/hwrender-portability-scope.md` §4, §11.1).

- **Upstream:** https://github.com/UZDoom/UZDoom
- **Branch:** `trunk`
- **Commit:** `7bfbf612d9d8197c36bb77ab171005bce521a514` ("Added -noverification", 2026-07-18)
- **Source path:** `src/common/rendering/`  →  here: `src/zandronum/rendering/`

## Why this is safe to adopt

Measured at this commit, the entire tree has **zero** references to `AActor`, `sector_t`, `seg_t`,
`player_t`, `VMFunction`, `PClass` — no game model, no ZScript. The only game-adjacent leak is
`FGameTexture` in 3 files of `hwrenderer/data`, bridged by our `features/hwrender` adapter. The
ZScript-coupled, float-sim-coupled scene layer (`src/rendering/hwrenderer/scene/`) is **deliberately
not vendored**; our own scene walker (which understands 64-bit `fixed_t` level data) emits into this
backend via `features/hwrender/scene_bridge`.

## Local modifications

None yet — pristine vendor. Any adaptation lives in `src/zandronum/src/features/hwrender/` (the
adapters/shims) so this tree stays a clean re-pull target. When a local edit here becomes
unavoidable, record it in this file with the reason.

## Re-pulling

```
cp -R <UZDoom>/src/common/rendering/. src/zandronum/rendering/
```
then diff against this pin and update the commit hash above.

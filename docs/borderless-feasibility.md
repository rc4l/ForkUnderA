# Borderless windowed: feasibility and plan

Requested feature: borderless-windowed ("fake fullscreen") plus every adjacent fix and the menu
consequences, targeted at the post-staircase renderer.

## Where the feature comes from (upstream provenance, pinned)

| Commit | Date | What it did |
|---|---|---|
| `938cd3cab` | 2017-12-07 | `win_borderless` — first borderless fake-fullscreen, Windows-only, 20 lines |
| `b65b83edb` | 2018-06-17 | **The keystone**: removed hard resolution switching everywhere; fullscreen *became* borderless-desktop. Net **−1,826 lines** — deletes `videomenu.cpp` (−386), the ZScript video menu, the mode iterators, and the per-backend mode-set machinery |
| `babe55819`..`af0e11f37`/`c6e4d6a33`/`37a0c1d6c` | 2018-06 | Fullscreen toggle per backend (Win32/SDL/Cocoa) + window position/size saver |
| `491898fe2` | 2018-06 | Crash fix: `screen` accessed before set during video init |
| `156ed5790` | 2018-07-21 | `vid_setsize` CCMD + window-restore fixes when leaving fullscreen |

End-state SDL implementation is one flag: `SDL_CreateWindow(..., fullscreen ?
SDL_WINDOW_FULLSCREEN_DESKTOP : 0)`; `IsFullscreen()` reads the window flag back.

## Why this is Route 2 (model reimplementation), not a diff apply

The 2018 diffs sit on four years of backend refactoring we do not carry (`DFrameBuffer` split into
`SystemFrameBuffer`, `IVideo` restructure) and the menu half deletes **ZScript** files
(`zscript/menu/videomenu.txt`) that our engine has never had. Attempting the diffs verbatim is
hopeless; reimplementing the *model* is small, because our menu is still C++ and our SDL backend is
already ours (`sdlglvideo` + `features/hwrender/computation/glcontext_compute`).

## The plan (`features/borderless-video/`)

1. **SDL backend (macOS/Linux)** — small. Fullscreen requests become
   `SDL_WINDOW_FULLSCREEN_DESKTOP`; the framebuffer takes the drawable size; the exclusive
   mode-set path is deleted; `vid_setsize` CCMD added; toggle via
   `SDL_SetWindowFullscreen`. `vid_adapter` CVAR for multi-monitor placement.
2. **Win32 backend** — moderate. Replace the `ChangeDisplaySettings` exclusive path with a
   borderless `WS_POPUP` window sized to the monitor (the `938cd3cab` pattern), plus the
   `af0e11f37` window-placement saver.
3. **Menu** — the mode grid retires (upstream deleted it outright). "Set Video Mode" becomes:
   Fullscreen (borderless) on/off, a windowed-size preset list (the existing `WinModes[]` table
   finally finds its true calling), and `vid_setsize` for arbitrary sizes. Pure C++ menu edits —
   *easier* for us than upstream's ZScript surgery.
4. **Renderer** — no work. The GL renderer is resolution-independent and simply renders at
   desktop size. (Later, upstream's `vid_scalemode` adds sub-native render scaling; separate
   follow-up.)

## Adjacencies and risks

- **Gamma**: hardware gamma ramps only ever applied in exclusive fullscreen. Borderless behaves
  like windowed today (gamma CVAR inert on some platforms) until post-process gamma is backported
  (a 2016-era feature; tracked follow-up). Silver lining: the classic "crash leaves the desktop
  gamma broken" failure mode *disappears* with borderless — there is no display state to restore.
- **Config semantics**: `vid_defwidth/height` become the windowed size; fullscreen ignores them.
  Trivial migration.
- **The 1920x1080 complaint dies permanently**: no exclusive mode list means no missing modes —
  every display shows its desktop, and windowed sizes are free-form.
- **Sim/netcode/fixed64/ZScript contact: none.** This is entirely draw/backend-side.

## Sequencing and estimate

Technically independent of the renderer replay — it touches the video backend, not `gl/scene`.
Recommended order all the same: land the flight-14 fix wave (or climb to `g2.0.05`-equivalent)
first so backend churn and renderer fix replay stay untangled. Estimated: one flight per backend
plus one for the menu — three flights, each with the usual gates and per-platform experimental
builds.

## Position clarification (pinned against real release tags)

Fetched from the canonical gzdoom repo into the reference clone: `g2.0.05` (stable 2.0,
2014-12-27) and `g2.1.1` (stable 2.1, 2016-02-23). Our summit `fc0cf4f99` is a **direct
ancestor** of both. Exact distances: stable 2.0.05 = **+107** src/gl commits from the summit;
stable 2.1.1 = **+203**. The staircase's "2.1-class" shorthand in earlier docs meant the 2.x
architecture line, not the 2.1 release itself — the ladder to the true stable is:
`g2.0.01pre` (+30) → `g2.0.05` (+107) → `g2.1.pre` region → `g2.1.1` (+203, at the wall's
doorstep).

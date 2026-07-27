# Renderer test maps

Verification instruments for staircase flights whose changed code MAP01 can't
exercise.

## lighttest — 3D-floor dynamic-light splitting

`lighttest_gen.py` emits a minimal Hexen-format PWAD (`/tmp/lighttest.wad`): a
tight room, a solid 3D-floor slab (`Sector_Set3DFloor`, z 48–80) crossing the
walls, and a `ShortRedTorch` (DoomEdNum 57 → `SMALLREDTORCH` dynamic light via
Zandronum's `doomdefs.txt`) on the floor. The torch light lands on the walls and
the 3D floor splits it into upper/lower light regions — this is the
`GLWall::SplitWall` / hardware-clip-plane 3D-light path (staircase flight 24,
upstream `fc57180d7`..`c129cb3ca`).

MAP01 has no 3D floors, so it never runs this code; a green MAP01 screenshot is
false confidence for that flight.

### Build & run

```
python3 progress/renderer-staircase/testmaps/lighttest_gen.py    # -> /tmp/lighttest.wad
build/ZandroX.app/Contents/MacOS/zandronum \
  -iwad "$HOME/Library/Application Support/Zandronum/doom2.wad" \
  -file /tmp/lighttest.wad +map MAP01 +god 1 +gl_lights 1 +gl_lights_size 3
```

The light radius (`SMALLREDTORCH` = 48) is small, so the room is tight and
`gl_lights_size 3` widens the reach to hit the walls.

### Baseline

`baseline-flight23.png` — the correct render at flight 23 (`712afbd`), before the
SplitWall rewrite: a bright torch-lit band across the wall middle with a crisp
horizontal 3D-floor light boundary and darker regions above/below. Flight 24 must
reproduce this. A/B by rebuilding pre/post and comparing the same spawn view.

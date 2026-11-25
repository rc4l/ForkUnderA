# Quiet missile steps skipped

A missile faster than its radius moves in sub-steps, each a full `P_TryMove`: a position check of the things and lines
near it, then an unlink and link. `P_XYMovement` takes `1 + speed / (radius - 1)` of them (3 for an imp ball, 5 for a
mancubus shot); `AFastProjectile::Tick` takes at least 8, doubling with speed.

Before stepping, the missile's whole move this tic is checked once: every thing and line in the blockmap cells that
cover all of its steps is gathered, and the steps are judged in order. A step is quiet when its box meets no thing that
`PIT_CheckThing` would act on and no line `PIT_CheckLine` would act on. It must also stay in the missile's sector,
and keep the missile between that sector's floor and ceiling. Such a step's `P_TryMove` can only move the missile.
Leading quiet steps are skipped, and the real loop resumes at the last of them, so every step from there on, and every
hit, explosion, rip and line special, runs exactly as before.

- **Only where it pays.** A sweep costs about one step's walk of the things and lines near it, so it runs only on a
  move of 4 or more steps. On the Sunder MAP11 laps, a skipped step cost 7,700-12,000 cycles at 2 steps and about
  4,200 at 3, the price of a real step, but 200-2,600 from 4 steps on.
- **Only where that holds.** The missile is not a player, not `NOCLIP`, `STEPMISSILE`, a floor or ceiling hugger,
  `CANTLEAVEFLOORPIC` or blasted, walks no slope, and wall running is off. Its sector has flat floor and ceiling, no
  3D floors, no deep water and is not underwater.
- **Fast projectiles.** A skipped step still does the rest of its iteration: the rip count, the height, the water
  level and the trail, at that step's position. A trail that lands in the blockmap ends the skipping there. Past a
  loud step the projectile survives (a bullet leaving its shooter, a two-sided line), the rest of its steps are swept
  again, up to three sweeps a tic.
- **What a skip leaves the same.** The missile ends at the head of its blocks' and sector's lists, with the sector
  list of its own sector only, which is what the skipped relinks would have left.

`computation/sweptmove_compute.*`: the rules. `sweptmove.*`: the sweep. Hooks: `p_mobj.cpp` (`P_XYMovement`, and
`AActor::StaticSpawn` counts blockmap spawns), `g_shared/a_fastprojectile.cpp` (`AFastProjectile::Tick`), `p_map.cpp`
(`P_MoverChecksThings`).

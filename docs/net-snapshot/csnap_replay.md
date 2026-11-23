# csnap_replay: the replay harness

Covers: src/zandronum/src/features/net-snapshot/csnap_replay.cpp, src/zandronum/src/features/net-snapshot/computation/replay_compute.h, src/zandronum/src/features/net-snapshot/computation/replay_compute.cpp, src/zandronum/src/features/net-snapshot/computation/replay_compute_test.cpp

## Responsibility

`fua_csnap_analyze` plays a `.csnap` ([record.md](record.md)) through the real decoder
(`ApplyDelta`) and the real sampler (`SampleGhost`), at the recorded arrival and render times and
under the recorded settings, and reports how the result moved, per segment and per entity. Given the
same file and settings the output is the same, so two configurations, or two builds, are compared on
identical input -- which is what the anchor comparison does with a committed fixture
([tools.md](tools.md)). It reproduces the sampler and smoothing; it has no mirrors, so no local
animation and no wall, floor or ceiling clamp, and it computes no render tiers. The design: [The
recorders](design.md#94-the-recorders).

## Use

```
fua_csnap_analyze <file>                      summary over the whole stream
fua_csnap_analyze <file> <id>                 the summary, and a per-frame trace of one entity
fua_csnap_analyze <file> csv <out>            the summary, and a per-entity CSV
fua_csnap_analyze <file> ... <setting>=<n>    any of the above with a setting overridden
fua_csnap_analyze <file> ... refill           any of the above, drawn as plan C would draw it
fua_csnap_analyze <file> ... backdate         refill, with a ghost entering mid-tic backdated
fua_csnap_analyze <file> ... anchor           refill, with every refill's start anchored to its fold
```

An id is 1-65535. A mode keyword counts in any position but as the CSV's file name. An override
names a whole-number or on/off setting of this build and holds over the recorded value for the whole
replay, segment changes included. The file is resolved in the module's own folder, and the replay
refuses to run while connected or playing a demo, since it clears the ghost table.

`refill` draws as the render copy would under plan C, which refills its actor records on a new tic
or an applied packet and blends every frame between a start and an end pose. A `.csnap` holds no
client gametic, so the client's tics are phased from the first frame, which is as representative as
any phase. A frame refills every present ghost when its tic is new, or when a delta, reset, map or
settings change came since the last refill. A record folds at the refill's frame, into the ghost's
smoothing as of its latest refill in the tic, which is what persists within the tic, so it folds
once and eases from the frame it arrived on, as on the default path; the tic's end is then sampled
and smoothed on from that state, and carries into the next tic. A refill on a new tic samples the
tic's start and smooths it on the state the tic began with. A refill mid-tic instead sets the start
so this frame's fraction draws the fold's own output, set as a rounded offset from the end so a fold
that lands on the end gives the end exactly; that output is where the default path draws the ghost
at this frame: its old position while a correction eases in, and the new one after a snap or a
reset, or on an unsmoothed ghost. The rest of the tic runs straight to the end, and within a
sixteenth of a tic of the end the end is drawn outright. Each frame draws the end pose, its state
included, with its position blended from the start (`FixedMul`, as the copy blends) and its ages
taken at the frame. The sampler takes whole milliseconds, so both edges are rounded and the fraction
is taken over the span actually sampled, which keeps a pose moving at its own speed within the tic.
A ghost not drawable at the tic's start draws its end pose all tic, as a new actor's `PrevX` is its
spawn position; `backdate` instead starts a moving one a span of its wire velocity behind its end,
so it is first drawn where the default path draws it. `anchor` applies the mid-tic rule at every
refill, a new tic's and an entering ghost's included, so no refill samples the tic's start and
nothing needs backdating. Run each mode and the default to compare.

## A run

1. Opens the file, saves the current value of every recorded and overridden setting, and applies the
   recorded ones, with the overrides on top. A recorded setting this build fixes at a constant, or
   does not have, is named in a line of its own.
2. Takes the ghost table (`ClientReset`, `GhostClearAll`), and installs its own hooks: no sinks, so
   the records they would take are counted as having none; an animates lookup that follows the
   recorded `fua_csnap_localanim` (every ghost animates at 3, none at 0, and 1 or 2 are reported as
   not reproducible); no gravity law until a map record matches the loaded level; and the narrow
   frame at origin 0 until a map record carries another. The recording's player is the local one.
3. Reads records in order:
   - **a frame carrying new settings** closes the segment, prints it, and applies the new values;
   - **a reset** clears the ghost table and every entity's history, and drops the law;
   - **a map record** installs the law (`LawAt`) only if its checksum matches the loaded level's,
     and its position frame (`UseMapFrame`) always, since the deltas after it are relative to that;
   - **a mirror record** stores what the game drew for that id;
   - **a delta** goes to `ApplyDelta`, its first error printed; an entity that entered starts its
     history over;
   - **a frame** samples every present ghost at the frame's time, smooths the pose (`ComputeSmooth`,
     [ghostactor.md](ghostactor.md)), and scores it, below; with `refill`, it draws from the ghost's
     last refill instead.
4. Prints the last segment and the totals, writes the CSV, restores the hooks and the saved
   settings, and clears the ghost table.

## What a segment reports

A segment is the span between setting changes, so an A/B recorded by flipping a setting mid-stream
reports each half apart.
- Active entities, mean record age and single-frame jumps per frame, and the clock model's line,
  whose window counters the replay drains when it starts, so nothing run before it is counted.
- **Ballistic draw error**: between two frames at one velocity a projectile should move velocity
  times the elapsed time; the miss is what the sampler added.
- **Worst step**: the largest ballistic draw error's id, class, time and sampler exit (under
  `refill`, the end pose's), and under `refill` how its start was set (the start pose's exit, "not
  drawable", or anchored) and how many frames after which kind of refill it fell. A per-entity trace
  prints each frame's exit too and marks the frames that refilled, and on those it prints the
  anchor's inputs in fixed units: the fold's output, the start and the smoothed end, each against
  the end (h-E, S-E), the smoothed end against its raw sample (E-raw), and the exit the sample at
  the frame took.
- **Ballistic accuracy**: the drawn position against where the server had it at the frame's model
  time, between the records either side (`ComputeHermite`); a frame before a record at rest is
  counted apart, and one whose next record never comes within 70 tics is unresolved, as is one still
  waiting when its segment ends or the recording resets.
- **Body accuracy**, without `refill` only: every frame of a body (no missile bit, not a player)
  whose pose carries a wire velocity, drawn against where the server had it at the frame's model
  time, the records either side joined by a straight line, as mean, p99, max and past 8 and 32 u,
  with its own unresolved count. Bodies are held at their records, so this reads how far behind the
  present a held body is drawn, at model time.
- **Dated ahead** and **smoothing**: the sampler's dated-ahead counts and the smoothing census over
  the segment. With `refill`, the census's folds, snaps and residuals count every fold, and its
  off-target, outside and moving counts count each client tic once, from its latest refill; the
  dated-ahead counts include every sample a refill takes. A line gives the refills, how many a new
  tic made due and how many a delta alone, the frames per refill, the ghost refills with no start
  pose and how many of those were backdated, and the ghost refills anchored and how many of those
  followed a snap or a fresh start. Under it, **smoothing by kind** gives the segment's frame rate
  and then a line per non-empty cell of the extrapolated census by kind (ballistic, turning, body)
  and by what dated the ages (the clock model or arrival): folds, snaps with how many jumped back
  and their sizes, REVERSED, and the frames drawn off course past 15 and 45 degrees. "turning" is
  the steering bit alone, so it also holds missiles not yet proven straight. Past 90 degrees is not
  printed: it is REVERSED plus the snaps back, since the off-course counts include snaps and
  REVERSED does not. The angles depend on the frame rate, so they compare settings within a
  recording only. At a threshold of 0 both still print, every correction counted as a snap, which
  under `refill` also counts as a snap for the anchored refills.
- **Glide**: displacement over 250 ms windows against what the class covers at its own speed
  (`SpeedForClass`), as a share bounded by `fua_csnap_glide_missile` or `fua_csnap_glide_monster` by
  the missile bits. A window an update landed in is SERVED, the sampler's account; one with none is
  STARVED, a late correction. It is a tripwire: a rise on the same recording flags a sampler change,
  while its level says nothing alone. It takes a window, not a frame, since a monster moves in
  `P_TryMove` steps several tics apart, and straight-line displacement, since a monster pacing back
  and forth is legitimate.
- The most-sampled classes' observed rates against their bound, and the classes of the worst
  windows.

The totals: deltas, frames, resets, decode errors and segments; a warning when the class ids seen do
not resolve, since ids are the recording's numbering and match only with the same files loaded in
the same order; and how often the replay's sprite agreed with what the game drew, which is the
self-test with `fua_csnap_localanim` 0 and the cost of the missing local animation otherwise. Those
last two lines are the class table's, so the anchor comparison leaves them out
([tools.md](tools.md)).

## Recorded settings

`replay_CvarName` maps a recorded name to the live one: a renamed setting through `g_replayRenamed`,
and a retired one, through `g_replayRetired`, to nothing, with a line when the recorded value
differs from the constant that replaced it (none for one that nothing read, `REPLAY_UNREAD`).
Retired names are matched first, ahead of an ini placeholder of the same name. A recorded setting
this build does not have is reported, since the replay is then not reproducing the recording. A
renamed setting maps to its live name in one step, so a recording made under any earlier prefix --
`snap_`, `clsnap_` -- replays with today's `fua_snap_` and `fua_csnap_` settings.

## Data structures

- `ReplayEntity`: one id's counts, its last drawn pose, its glide window, and its newest position
  records for the accuracy check.
- `ReplaySegment`: one segment's frames, error histograms and smoothing census.
- `ReplayClassRate`: the observed rate per class over a segment's served windows.
- `ReplayAccWait`: a drawn frame waiting for the record after it.
- `ReplayRefill`: one ghost's smoothed start and end poses from its last refill under `refill`, the
  sampler's exits for both, how the start was set, its smoothing as of its latest refill, and the
  drawn counts that tic's latest refill added to the census.
- `ReplayOverride`: one setting from the command line.

## Public functions

The command is the file's only entry. Its statistics are `replay_compute`, a pure unit:
- `ComputeDistUnits`: the distance between two positions in whole units, which the glide census
  measures.
- `ComputeBinAdd`, `ComputeBinP99`: an error into a histogram of `SNAP_REPLAY_BINS` 0.1 u bins, the
  last holding everything past, and the p99 as the upper edge of the bin holding the sample at 99%
  of the count.
- `ComputeCensusAdd`, `ComputeCensusSplit`: a smoothing census added into another or taken back out,
  and split into the fold half every refill adds and the drawn half a tic's latest refill replaces.

The start a mid-tic refill sets is `ComputeAnchorStart`, in the smoothing unit the mirrors share
([ghostactor.md](ghostactor.md)), so the replay and the game set it the same way.

## Settings

Client only, unarchived: `fua_csnap_glide_missile` (125: percent of the class's speed a missile may
sustain), `fua_csnap_glide_monster` (55: the same for everything else). They are content
calibration, not sampler settings.

The command: `fua_csnap_analyze`.

## Rules that must hold

- **The real decoder and sampler, never a copy.** A harness that disagrees with the game is worse
  than none.
- **Everything taken is put back**: the settings, the hooks, the map's frame and the ghost table.
- **Settings by name**, as the recorder writes them; a renamed or retired one goes in the two
  tables, never silently.
- **Per map, the law only on a matching level.** It is drawn against the loaded level's sectors.
- **Under `refill`, a refill never starts from the end of its own tic**: a record folds at the
  refill's frame into the state kept within the tic, so it folds once, and a tic's drawn counts come
  from its latest refill.
- **A record the loop does not handle is counted and skipped**, never sampled as a frame. The reader
  itself stops at a kind it does not know.
- **Two runs of one file print the same figures.** That is what makes an A/B, and the anchor
  comparison against the source tree, mean anything.

## Engine hooks

None: a console command.

## Tests

`computation/replay_compute_test.cpp` is a GoogleTest file under the 100% line gate and takes the
unit's lines and branches but one, `d < 0.0`, which `sqrt` never returns: the distance in whole
units in every direction, each histogram bin's edge, the last bin, the p99 either side of 99% and
with nothing counted, a census sum taken back out, and a split's halves adding back to the whole.

Two replays of one file under the same settings print the same figures; that is what makes an A/B
meaningful, and `tools/snapshot/snap_anchor_compare.py` holds an `anchor` run to the source tree's
own over the committed reference recording. With a recording made at `fua_csnap_localanim` 0, the
drawn-state line reads close to full agreement. With `refill`, two runs of one file print the same
figures too.

# selftest: fua_snap_verify and fua_snap_selftest

Covers: src/zandronum/src/features/net-snapshot/selftest.h, src/zandronum/src/features/net-snapshot/selftest.cpp

## Responsibility

Two console commands that check the snapshot code without a connection, kept out of `entities.cpp`
so the ghost table carries no file I/O or test code:

- `fua_snap_verify`: validates a `.zsnap` server recording through the client's own decoder and
  clock model, and reports what it found.
- `fua_snap_selftest`: runs the self-test blocks a game binary is needed for -- the time base
  (`formattest.cpp`) and the ghost-table and decode blocks (`entities.cpp`) -- and prints the blocks
  that ran. The codec cases themselves are GoogleTest files now ([formattest.md](formattest.md)).

## Data structures

The block list: the names of the blocks run since the last reset, comma-joined in fixed storage,
since a console command must not depend on an allocator. A name that does not fit becomes a marker,
so an overflowed list never reads as complete.

## Public functions

- `VerifyFixture`: reads a recording with `ZsnapReadRecord` and feeds each record to `ClockSample`
  and `ApplyDelta`, with an optional clock skew applied before any timestamp is taken. It reports
  the record and entity counts, the baseline, the clock estimate and the session rate, and returns
  an exit code matching `zsnap_info.py`: 0 PASS, 2 errors, 3 aborted at an undecodable record, 4
  valid with a truncated tail ([tools.md](tools.md)). It clears the ghost table and the clock model
  when it starts and when it ends, and sets the decoder's stream version and map frame from the file
  (`SetStreamVersion`, `UseMapFrame`), putting the frame back at the end. A record the decoder
  counts as redundant (an enter for an id already present, a leave for one absent) is an error, as
  in `zsnap_info.py`: the recorder writes world diffs from a baseline, never a re-sent copy, so one
  means presence went wrong. So is a record it counts as late (older than a group's stamp), as a tic
  going back is in `zsnap_info.py`: the recorder's tics only go forward. Only `.zsnap` files reach
  it; the live stream's re-sent and late records go through `ApplyDelta` alone.
- `RunFormatSelfTests` (in `formattest.cpp`) and `RunSelfTests` (in `entities.cpp`): the two entry
  points `fua_snap_selftest` runs.
- `SelfTestBlock`, `SelfTestBlocksReset`, `SelfTestBlocks`: each block names itself as it runs, and
  the summary prints the names and their count.
- `SelfTestPrint`: output for the format blocks, `Printf` in the engine and stdout where the same
  code is built without one.

## Settings and commands

- `fua_snap_verify <file.zsnap> [skew]`: the file name may hold no path separator and is resolved in
  the module's own folder; skew is in (0.01, 100] and stretches the recording's wall-time intervals
  by that factor, to test the clock model against a server running slower or faster than 35 Hz.
- `fua_snap_selftest`: prints `fua_snap_selftest: PASS (N blocks: ...)` or the failure count, and
  the decode hash on every run.

## Rules that must hold

- **Every block names itself.** A block added without `SelfTestBlock` is missing from the list, and
  the count is what makes that visible. The ghost-table blocks refuse a live table and say so, so
  they are absent from the list while connected rather than failing -- which is why the selftest is
  run before connecting, the decode check being one of them ([entities.md](entities.md)).
- **Verification leaves nothing behind.** `VerifyFixture` resets the ghost table and the clock model
  at both ends, so it refuses while connected or playing a demo, whose table it would clear.
- Its clock samples count toward the client's interval figures, so the next `[snap] clock` line
  includes the recording.

## Engine hooks

None. Both commands are registered by `CCMD` in this file.

## Tests

`fua_snap_selftest` is itself an entry point, and CI runs it in the engine on every push -- the
Linux dedicated server as a required step, macOS and Windows beside it -- reading the verdict and
the decode hash from the log ([entities.md](entities.md), the decode check). `fua_snap_verify` on a
known recording should print the same counts from one build to the next; a change there without a
format change is a decoder change, and `tools/snapshot/zsnap_info.py --decode` reads the same file
independently ([tools.md](tools.md)).

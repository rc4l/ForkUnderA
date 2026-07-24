<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 rc4l -->

# Fuzz corpus

[rc4l] FuzzTest's corpus database for `zandrox_fuzz`. Two kinds of input land here, both
committed on purpose:

- **coverage** — inputs the nightly run found that reach a branch nothing else reached. Replaying
  them (`tests/fuzz.sh --replay`) re-checks those paths in seconds, without re-fuzzing.
- **crashing** — a counterexample that failed a property. Commit it with the fix, exactly like the
  failing test you would have written by hand for a crash report.

Layout is `zandrox_fuzz/<Suite>.<Property>/`, created by FuzzTest itself — don't hand-arrange it.
The nightly workflow uploads this directory as an artifact, so a finding is recoverable even if
the run fails before anything is committed.

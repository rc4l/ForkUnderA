---
name: retro
description: A mini retrospective after a piece of work lands or a session's fixes are in — six fixed questions (CLI gaps, stale skills, dead code and long comments, anti-patterns, coverage and shared helpers, drift-prone docs), each answered from the diff and the session's own friction, then every finding fixed in the same pass and merged. Use when the user says "retro", "mini retro", "what could we have done better", or asks to review a session's changes for patterns before merging.
---

# Mini retro

A retro is not a write-up: it is a checklist run against what just happened, with every finding
fixed before the branch merges. The six questions are fixed so the answers are comparable across
sessions. Answer each from evidence (a grep, a diff, a command that was run), never from memory of
how things "usually are".

## Inputs

- The diff: `git diff main...HEAD --stat` plus the uncommitted tree.
- The session's friction: every error message read, every command retried, every wrong guess at a
  syntax, every fact that had to be looked up twice. Each is a candidate CLI, skill or doc fix.

## The six questions

1. **CLI upgrades.** For each moment the tooling (`tools/fuactl`) made the session slower, ask whether
   the tool could have said it. A failure whose cause sat in a file the tool knew about (a crash log, a
   missing submodule, a usage) is a tool gap. Fix the tool, with a `node --test` case, rather than
   writing the workaround into a skill.
2. **Stale skills.** `grep` each path, flag, command and binary name the session used against
   `.claude/skills/*/SKILL.md` and the memory folder. Anything that names a removed tool, an old binary
   or a duplicated table is updated or pointed at the one owner (a table that exists in two skills will
   drift; keep one and link it).
3. **Dead code and long comments.** `node tools/fuactl/src/cli.mjs comments --since main` for comment
   length. For dead code, grep every copy of a thing the session touched: an "overlay", a mirror, a
   script nothing calls. A copy nothing references is deleted, not updated.
4. **Anti-patterns.** For each change, name the established pattern it should follow (the nearest
   existing code doing the same job) and check it does: `Compute*` helpers for logic, feature folders
   for new files, one predicate where two places decide the same thing, `[rc4l]` one-sentence comments.
5. **Coverage and shared helpers.** Every branch the session added is in a `*_compute.cpp` with a
   `*_compute_test.cpp`, or in a `.mjs` with a `.test.mjs`; `fuactl coverage --since main` on Windows,
   `tests/coverage.sh --auto` elsewhere. Then look for the same decision made in two places (two
   platforms' ship lists, two scripts' submodule handling) and either share it or make the tool check
   that they agree.
6. **Drift-prone docs.** A comment or doc that restates a value, a list or a path that lives elsewhere
   will go stale; replace it with a pointer to the owner, or a check that reads the owner.

## Output

- Every finding fixed in the same branch, each as its own commit, with the tests that prove it.
- A short list in the PR: question → finding → fix, one line each; "nothing" is a valid answer when
  the grep was run and came back empty, and the grep is named.
- New facts about the environment (not derivable from the repo) go to memory; facts about the code go
  into the code's own comments or README, never into memory.

## Worked example (2026-10-21, mac from main)

| question | finding | fix |
|---|---|---|
| 1 | `play` said "bridge closed"; the crash file beside the log had the stack | `crashedError` names the file and frames; `summarizeCrash` reads macOS frames |
| 1 | `png --crop` with the wrong word order surfaced as a decoder error | `pngShortUsage` per mode |
| 1 | configure stopped on a missing submodule nothing had checked | `doctor` names it; `mac_compile.sh` checks it out |
| 2 | `zandronum-driver` carried its own copy of the build table | points at `zandronum-rebuild` |
| 3 | `tools/mcp-bridge/overlay` was a stale copy of `mcp_*.cpp` | deleted |
| 4/5 | `dumprenderer` decided "GL is live" twice, untested | `ComputeGlLive`, `ComputeRendererName` + test |
| 5 | three platform scripts each list what ships; mac's lacked `iwads/` | `doctor` checks `iwads/` beside the binary |
| 6 | a memory described the old MCP server's fixed port | rewritten for fuactl |
| 1 | `writing-tests`' own build command lost the test binary to CMake's 5 s discovery timeout on a cold sanitized build | `DISCOVERY_TIMEOUT 120` on every `gtest_discover_tests` |

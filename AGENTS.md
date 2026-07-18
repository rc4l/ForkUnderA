Keep src/ at 100% test coverage for all new c++ code (statements, branches, functions, lines) added. No copping-out. If we are touching code that didn't have tests before, then make new tests. Use crashes as opportunities to write rigid tests; ie write failing tests to prove the crash and then write code to fix the crash and naturally the test should pass.

Don't write anti-patterns. Prefer generic, reusable helpers over hand-maintained boilerplate that grows per case. If you catch yourself copy-pasting a function for each new command/parser/tool, factor out the shared part instead.

When a feature is mostly new files, group its source, headers, and tests together in a subfolder under `features/` (package-by-feature) inside `src/zandronum/src/` and are listed in that feature's README. See `features/README.md`.

Comments should be one sentence maximum with each comment being prefixed with the github account name. For example 'rc4l' becomes '// [rc4l]'

When using Zandronum MCP, prefer windowed mode as opposed to fullscreen so we can work on other things in parallel.


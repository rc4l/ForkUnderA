Keep src/ at 100% test coverage for all new c++ code (statements, branches, functions, lines) added. No copping-out. If we are touching code that didn't have tests before, then make new tests. Use crashes as opportunities to write rigid tests; ie write failing tests to prove the crash and then write code to fix the crash and naturally the test should pass.

Extract as much logic as humanly possible into pure, dependency-free computation helpers so it can be unit-tested without linking the engine — leave only thin glue (engine globals, AL/decoder/OS calls) around them. Every such extracted helper MUST be named with a `Compute` prefix (e.g. `ComputeGravityOffset`, `ComputeMidiDeviceDefault`, `ComputeFloatToBytes`) so testable computations are recognizable at a glance and get colocated `*_test.cpp` coverage. When a computation must sit on top of a C API (e.g. OpenAL `al*`/`alc*`), mock that one interface with GoogleMock rather than pulling in the engine.

Don't write anti-patterns. Prefer generic, reusable helpers over hand-maintained boilerplate that grows per case. If you catch yourself copy-pasting a function for each new command/parser/tool, factor out the shared part instead.

When a feature is mostly new files, group its source, headers, and tests together in a subfolder under `features/` (package-by-feature) inside `src/zandronum/src/`, and list its in-engine hooks in that feature's README. Feature `.cpp` files that use `IMPLEMENT_CLASS` MUST be added to the `add_executable( zdoom … )` list before `zzautozend.cpp` (not appended via a trailing `target_sources`), or their class registration lands outside the linker's `creg` section and the class silently never registers — see `src/zandronum/src/features/README.md`.

Comments should be one sentence maximum with each comment being prefixed with the github account name. For example 'rc4l' becomes '// [rc4l]'

When using Zandronum MCP, prefer windowed mode as opposed to fullscreen so we can work on other things in parallel.

Keep pull request descriptions concise and minimal. Don't write an essay.

Don't modify the base README file without explicit permission.
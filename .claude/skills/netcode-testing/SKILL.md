---
name: netcode-testing
description: How to write deterministic, runnable tests for ZandroX's client/server wire format — golden byte fixtures, read/write round-trips, and adversarial buffer-overflow/truncation cases at the bit and byte level, all as fast unit tests with no live server. Use whenever adding or changing a SERVERCOMMANDS_* command, touching the BYTESTREAM_s serialization layer, or asked to prove "is this networking correctly?"
---

# Testing the ZandroX netcode wire format

Multiplayer bugs here are silent: a one-byte encoding drift makes the client misread every following
byte of the packet, and it passes a single-player build+run clean. So we test the **wire** — the exact
bytes on the stream — deterministically, as unit tests, with no processes and no sockets. This skill is
how. It is the machine-checkable half of the `netcode-adaptation` skill's "wire-format regression
tests" rule.

## The rigid core: `BYTESTREAM_s`, not the command table

Every one of the ~231 generated `SERVERCOMMANDS_*` reduces to a handful of primitives on
`BYTESTREAM_s` (`networkshared.cpp`) — `WriteByte/Short/Long/Float/String/Bit/ShortByte/Variable/
Buffer` and their `Read*` mirrors — plus a few field-layout conventions (little-endian ints, IEEE-754
floats, LSB-first bit packing, 2-bit-length varints, NUL-terminated strings, `short(fixed >> FRACBITS)`
fixed-point). Pin the atoms and every command's encoding is pinned with them.

**Why not test the commands directly?** The generated `CLIENT_ParseServerCommand` (servercommands.cpp)
resolves actors/players/sectors against live engine state (`CLIENT_ReadActorFromNetID`, `&players[...]`)
and calls `Execute()` — it cannot be unit-linked without the whole engine. So: test the byte layer as
units (this skill), and cover per-command *behaviour* with the in-process replay harness (separate,
heavier). The command's *field layout* is still covered here as a pattern test.

## The target: `zandrox_tests_net` (already wired)

The reference test lives at `src/zandronum/src/networkshared_nettest.cpp` — **colocated** beside the
code it covers, like every other test. The `_nettest.cpp` suffix (not a bare `_test.cpp`) routes it to
the standalone `zandrox_tests_net` target instead of the monolithic `zandrox_tests`, because its
unit-under-test is vendored engine code that needs the `tests/shims/net/i_system.h` shim (`#define
Printf printf`, `#define atterm atexit`) placed first on the include path — the masterserver recipe.
New wire tests are new `*_nettest.cpp` files beside their subject; CMake re-globs, no edit needed.
`gtest_discover_tests` registers them, so `ctest` (and CI's Build+test steps) run them with no
workflow change. Runs under ASan+UBSan on Linux and macOS in milliseconds.

## Every wire test carries three kinds of assertion

1. **GOLDEN — the exact bytes.** Build the value, capture the buffer, assert the byte sequence
   literally. This is the contract; a refactor that shifts a field's width, order, or endianness fails
   *here*, on the exact field, instead of desyncing in the field at runtime. `WriteShort(0x1234)` →
   `{0x34, 0x12}`; `WriteFloat(1.0f)` → `{0x00,0x00,0x80,0x3F}`; `WriteVariable(5)` → `{0x01, 0x05}`
   (2-bit length prefix, then the byte). Golden encodes *current* behaviour — read the impl in
   `networkshared.cpp` before asserting, never guess the bytes.
2. **ROUND-TRIP — write then read, across boundaries.** `Write*(x)` → `Read*()` == `x` for 0, 1, -1,
   type min/max, empty and max-length strings, and bit sequences that span a byte boundary. Catches a
   writer/reader width mismatch (a `WriteShort` paired with a `ReadByte`) that golden bytes alone miss.
3. **ADVERSARIAL — a hostile or truncated packet stays in bounds.** This is the buffer-overflow /
   exploit surface, and it MUST run under ASan+UBSan (`ZANDROX_TESTS_SANITIZE=ON`) so an out-of-bounds
   access is a hard failure, not a silent pass.

## The bit-and-byte-level checklist for a new command's fields

For each field type the command serializes, there must be a test that it:

- **Byte/Short/Long:** little-endian; truncates to its width (`WriteByte(0x1FF)` → `0xFF`); reading past
  the stream end returns the `-1` sentinel and advances the pointer *without reading OOB*; a partial
  multi-byte read (2 bytes wanted, 1 present) is refused whole, not half-invented.
- **Float:** the raw IEEE-754 bit pattern round-trips exactly (compare bits, not `EXPECT_NEAR`).
- **String:** raw bytes + a single trailing NUL; `nullptr`/empty → one NUL; **write** refuses a string
  over `MAX_NETWORK_STRING` (2048); **read** of an over-long or *unterminated* string truncates into
  its fixed buffer without overflowing and keeps consuming so the packet stays aligned to the next
  field (verify the next field still reads correctly — this is the classic stay-in-sync exploit case).
- **Bits (`WriteBit`/`WriteShortByte`/`WriteVariable`):** LSB-first packing; the 9th bit rolls to a new
  byte; `WriteShortByte` masks the value to its bit width; reading bits past the stream end falls back
  to a zero byte (no OOB); bit fields interleaved with byte fields stay aligned.
- **Fixed-point:** the `short(fixed >> FRACBITS)` / `short << FRACBITS` convention round-trips whole map
  units exactly (model it with `* kFracUnit`, not a signed `<< 16`, or UBSan flags the *test*).

## Traps that have bitten (learn from them)

- **Engine macros collide with local names.** `doomtype.h` defines `FRACUNIT`, `FRACBITS`, etc. as
  macros; a local `constexpr int FRACUNIT` expands to garbage (`expected ')'`). Name locals distinctly
  (`kFracUnit`). Any wire test includes `doomtype.h` (before `networkshared.h`, for `BYTE`/`USHORT`),
  so its whole macro namespace is live.
- **The test must be UB-clean itself.** CI runs UBSan with `halt_on_error=1`. `-1 << 16` is signed-shift
  UB *in the test*, even though the engine does the shift in shipping code — model the same round-trip
  with multiplication so the test is clean.
- **`ReadString` returns a pointer to a `static` buffer** — fine for one-at-a-time asserts, but don't
  hold two results at once expecting them to differ.
- **Golden is a change-detector, not a correctness oracle** for the generated commands: encode and
  decode come from the same spec, so a spec *error* round-trips happily. The golden *bytes* are what
  catch it — which is exactly why every field needs a byte-level golden, not just a round-trip.

## What this layer does NOT cover — say so, don't pretend

- **Broadcast-completeness:** whether the gameplay code actually *called* the broadcast. No wire test
  sees an un-sent command; that stays with the `netcode-adaptation` rules + review (a static tripwire
  is a possible future guard).
- **Per-command apply behaviour under a real network** (latency reorder, loss, N clients): that is the
  in-process capture/replay harness, not this. Network conditions there are *inputs* (drop/reorder the
  captured command buffer), still deterministic, still no live server.
- **The socket/handshake transport:** inherited Zandronum code, rarely touched; covered by the
  occasional manual `zandronum-driver` session, not automated here.

Determinism is the whole point: a red wire test is a byte-level repro that replays identically on a
laptop, not a flaky multiplayer session. Keep it that way — no clocks, no RNG, no processes.

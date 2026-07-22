---
name: fixed64-widening
description: How to find and fix engine regressions caused by widening fixed_t from 32-bit 16.16 to 64-bit 48.16 in ZandroX. Use whenever a gameplay/render/movement/hitscan bug is suspected to come from the fixed_t widening, when auditing code that assumes fixed_t is 32 bits, or when the symptom is "wrong at non-cardinal angles / on giant maps / on far targets."
---

# Fixing 64-bit fixed_t widening regressions

ZandroX widened `fixed_t` from 32-bit **16.16** to 64-bit **48.16** (`FRACBITS` stayed 16,
so in-range math is bit-identical). The flip surfaced a long tail of latent bugs. They look
unrelated on the surface — bullet puffs in the wrong place, players sliding south, view spin
on right turns, doors flung across the map — but they are **all one bug told many ways:**

> **A piece of code assumed `fixed_t` is exactly 32 bits. Widening silently changed its result.**

The value of this skill is that you do **not** need to remember the ~15 fix commits. You need
the **six shapes** below, the **tools that surface them**, and the **fix protocol**. The real
commits are worked examples in `references/prior-art.md` — reach for them only to see a shape
in the flesh.

Read `references/prior-art.md` for the shape→commit map. Follow the `writing-tests` skill for
the test mechanics, and `AGENTS.md` for the `Compute*`/100%-coverage rules this fix protocol
depends on.

---

## Ground truth before you touch anything

**The type.** `fixed_t` is `zx::Fixed` (strong type, `features/fixed64/computation/fixed_strong.h`).
It wraps an `int64_t`, zero-cost after inlining, but rejects lossy conversions at compile time:
- Construction from `int`/`long`/`long long` is implicit; from **unsigned is explicit** (so an
  `angle_t`/`DWORD` source is a compile error until you write `Fixed(x)` = "I take responsibility").
- Construction from `double`/`float` is **explicit** and truncating.
- Conversion **to** an integer is `explicit` (`int i = fixedExpr;` is a compile error).
- `x.Raw()` gives the raw `int64_t`; `Fixed::FromRaw(int64_t)` goes back.
- `Fixed op <floating>` for `* / % << >>` (and `*= /= %=`) is **SFINAE-deleted** — a truncating
  float multiply is a hard compile error, not a silent runtime zero.

**What is still 32-bit ON PURPOSE — do NOT widen these:**
- **The network wire.** The protocol is 32-bit and symmetric on both ends; `protocolspec`
  emits explicit `fixed_t -> wire` narrowing. Widening it desyncs clients.
- **The ACS 16.16 API.** Scripts see 16.16; keep the boundary 32-bit.
- **On-disk map data** (`p_buildmap`, `p_glnodes`, `FileReader::operator>>(fixed_t&)`): map
  lumps store 32-bit fixed. Read as 32-bit and sign-extend; do not `sizeof(fixed_t)` a lump.
- **The savegame format is versioned, not auto-migrated.** If a change alters how many bytes a
  `fixed_t` serializes to, **bump `SAVEVER`/`MINSAVEVER`** (see `LAST_FIXED32_SAVEVER`) — do not
  assume old saves still load.

If your "fix" touches any of the four above, stop: you are probably widening something that is
meant to stay narrow.

---

## The six shapes (this is the whole taxonomy)

| # | Shape | The tell in the code | Why widening breaks it | The durable fix |
|---|---|---|---|---|
| 1 | **Truncation before use** | a `fixed_t` stored into `int`/passed to an `int` param *before* being reduced | past ~32k map units the value exceeds `INT32_MAX` and the `int` truncates (often sign-flips) | widen the var/param; extract the reduction into a tested `zx::Compute*` |
| 2 | **sizeof / width assumption** | `memcpy`/`sizeof(angle_t)`/on-disk struct fields typed `fixed_t` | the byte count or struct stride was pinned to 4; now the element is 8 | size the copy off the element's *own* type; retype on-disk fields to `SDWORD` |
| 3 | **Unsigned reinterpreted as widened-signed** | `angle_t` deltas, `& 0xXXXXXXXX` 8-hex-digit mask literals | a 32-bit unsigned value that used to reinterpret as a small negative now zero-extends to +4.2e9; a 32-bit mask zero-extends and wipes the sign bit | reinterpret as `int32` first (`InterpolateAngleBAM`); sign-preserving `AlignDownPow2` instead of a mask |
| 4 | **Wrap reliance** | a bare `*` that *wanted* the 32-bit product to overflow (compat slopes, some bot math) | the product is now full-magnitude, not wrapped | make the wrap explicit: `zx::Mul32Wrap` |
| 5 | **Overload flip (strong type)** | `FRACUNIT * sin(x)` / `Fixed * <float>` / integer-literal calls tied between a `fixed_t` and a `double` overload | `FRACUNIT` is now a strong `Fixed`, so `Fixed * double` bound to `Fixed * int` and truncated the float to 0 (this **zeroed the whole finesine table** → puffs/movement/sight/render dead at non-cardinal angles) | close it in the type (already SFINAE-deleted); at call sites wrap the intended float math as `double(...)`; cast ambiguous integer-literal args to `fixed_t` |
| 6 | **Signed-shift UB** | `a << n` where `a` is signed and can be negative | left-shifting a negative is undefined behaviour; UBSan flags it | multiply by `1 << n` (positive shift, then a defined signed multiply) |

Symptom → shape shortcuts:
- **"wrong only at non-cardinal angles"** → shape 5 or 2 (a trig table got zeroed/corrupted).
- **"wrong only on a right turn / on half the vertices"** → shape 3 (sign lost via unsigned/mask).
- **"wrong only far away / on giant maps"** → shape 1 (truncation past 32k units).
- **"builds fine, wrong at runtime, no warning"** → shapes 1/3 (implicit narrowings are silent).
- **"UBSan fails but local ctest passes"** → shape 6 (see the halt_on_error gotcha below).

---

## Detection playbook (how these are actually found — not by reading carefully)

**1. The `-Wshorten-64-to-32` sweep — your primary tool.** This flag enumerates every implicit
64→32 narrowing, which is where shapes 1 and 3 hide. It is **Clang-only** and is **not** in the
default build, so turn it on for a one-off sweep and triage the output:

```bash
# Clang only. Reconfigure the engine with the flag and collect the warnings.
cmake -S src/zandronum -B /tmp/sweep -DCMAKE_CXX_FLAGS="-Wshorten-64-to-32" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DNO_FMOD=ON >/dev/null
cmake --build /tmp/sweep 2>&1 | grep -E "shorten-64-to-32" | sort -u
```

Most hits are benign (in-range constants: friction, screen columns, velocity thresholds). Triage
each: does the value ever exceed 32-bit fixed in normal play (a coordinate, a distance, a delta)?
If yes, it is a shape-1 bug. `abs(fixed_t)` correctly binds the 64-bit overload — don't "fix" it.

**2. UBSan with `halt_on_error=1` — for shape 6.** ASan+UBSan is already in the test build
(`tests/CMakeLists.txt`: `-fsanitize=address,undefined`). **Gotcha:** local `ctest` prints the
UBSan diagnostic but does **not fail** the test without `halt_on_error`. CI sets it
(`_test.yml`: `UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1`). Reproduce locally the same way:

```bash
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build-tests --output-on-failure
```

**3. Targeted greps — for shapes 2, 3, 4.** Each shape has a signature you can grep:

```bash
S=src/zandronum/src
grep -rnE "memcpy\(|sizeof\(angle_t\)|sizeof\(fixed_t\)" $S           # shape 2
grep -rnE "0x[0-9A-Fa-f]{8}\b" $S                                     # shape 3 (32-bit masks)
grep -rnE "\b(FixedMul|MulScale[0-9]*|DMulScale[0-9]*|Scale)\b" $S    # shapes 3/4 (audit operands)
grep -rnE "fixed_t[^;]*;\s*//.*(on-disk|lump|wire)" $S                # shape 2 (on-disk fields)
```
For the `FixedMul`/`MulScale`/`Scale` family, check every operand: is any operand an **unsigned**
quantity (angle delta) or a value that can exceed 32-bit fixed? Those are the shape-3/4 sites.

**4. Field report → reproduce → pin.** A bug report like *"moving forward near -5° forces south"*
becomes a **failing test first** (pin the exact wrong output), then you fix until it passes. Never
fix a widening bug without a test that fails before the fix.

---

## Fix protocol (do all six steps, every time)

1. **Extract the fragile math into a pure `zx::Compute*` helper** in the nearest
   `features/**/computation/` (or `computation/`) folder — standalone, only `<cstdint>`, no engine
   headers — so it is unit-testable off-engine. This is the `AGENTS.md` `Compute` rule.
2. **Write the regression test that reproduces the pre-fix wrong value**, then make it pass.
   The test must fail on the old code (prove you found the real bug), and it pins the corrected
   value so the class can't silently return.
3. **Make reintroduction impossible — prefer a type/helper-level fix over a spot patch.** A deleted
   operator (shape 5), a sign-preserving helper (`AlignDownPow2`, shape 3), an element-type-sized
   fill (`FillFineSineWrap`, shape 2) fixes the *class*; editing one call site fixes one instance.
4. **Preserve intended semantics explicitly, never by accident.** If 32-bit wrap was the point,
   spell it `Mul32Wrap`. If a float multiply was the point, spell it `double(...)`. If a value must
   stay 32-bit (wire/ACS/on-disk), keep the explicit narrowing.
5. **If serialization width changed, bump the version guard** (`SAVEVER`/`MINSAVEVER`, and a
   `static_assert` so the version can never again lag a width change).
6. **Verify** (all four): engine builds green (arm64, OpenAL linked), the new test fails-then-passes,
   ASan/UBSan clean under `halt_on_error=1`, and one runtime check on `MAP01` reproducing the
   original symptom (e.g. face the bad angle and confirm forward thrust now goes the right way).

---

## Helpers that already exist — reuse, don't reinvent

All in `zx::` under `features/fixed64/computation/`:

| Helper | Signature | Use for |
|---|---|---|
| `ComputeMulDivS64` | `int64_t(int64_t a, int64_t b, int64_t c)` | generic `Scale(a,b,c)` = `a*b/c` via 128-bit; the scale-layer core |
| `Mul32Wrap` | `int32_t(int64_t a, int64_t b)` | shape 4 — reproduce a *deliberate* 32-bit product wrap (compat maps) |
| `AlignDownPow2` | `int64_t(int64_t v, unsigned bits)` | shape 3 — sign-preserving align-down, replaces `v & 0xFFFF....` |
| `FillFineSineWrap` | `void(T* finesine, int fineAngles)` | shape 2 — fill the finesine wrap tail sized off `T`, not a hardcoded 4 |
| `InterpolateAngleBAM` | `uint32_t(uint32_t old, uint32_t new, int64_t frac)` | shape 3 — interpolate an angle delta by reinterpreting it as `int32` first |
| `ComputePusherSpeed` | `int(int64_t dist, int magnitude, int pushFactor, int fracBits)` | shape 1 — point-pusher falloff reduced from a possibly-huge distance |
| `ComputeSeekerVelZ` | `int64_t(int64_t dist, int64_t speed, int64_t zdiff)` | shape 1 — homing-missile Z easing over a far distance |

If your bug is a new instance of a shape one of these already covers, call the helper. If it is a
genuinely new shape, add a new `Compute*` next to these with its own colocated `*_test.cpp`.

---

## The one-paragraph version

Widening `fixed_t` to 64-bit broke code that assumed it was 32-bit. There are six shapes:
truncation, sizeof/width, unsigned-as-signed, wrap-reliance, overload-flip, signed-shift-UB. Find
them with a `-Wshorten-64-to-32` sweep, UBSan `halt_on_error=1`, and greps of the `FixedMul` family
and 8-hex-digit masks. Fix them by extracting a pure `zx::Compute*` helper, writing a test that
reproduces the wrong value first, and closing the class in the type — never a lone spot fix. Keep
the wire, ACS, on-disk map, and savegame-version boundaries deliberately 32-bit.

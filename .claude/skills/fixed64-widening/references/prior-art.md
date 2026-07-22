# Prior art: the fixed_t widening fixes

Worked examples of each shape in `SKILL.md`, from the `feature/fixed64-scale` history. Use these
to see a shape in real code (`git show <hash>`), not as a to-do list. Commit hashes are on `main`.

## The flip itself

- **`5ad7a96` — flip fixed_t to 64-bit (48.16) and fix the engine.** The base change:
  `FRACBITS` stays 16, so in-range math is bit-identical and the deterministic sim is unaffected.
  Math layer (`fixedmath.h`, `m_fixed.h`) delegates to the tested `zx::` scale layer. Also fixed
  the first batch of interior breaks: `FileReader::operator>>(fixed_t&)` sign-extends 32-bit
  on-disk fixed; `p_buildmap`/`p_glnodes` map structs retyped to `SDWORD` (latent stride bugs);
  `ZatPoint(0,0)` integer-literal calls cast to `fixed_t`; `templates.h` MIN/MAX/clamp got
  arithmetic-only mixed-type overloads constrained by `is_arithmetic`.

## By shape

| Shape | Commit | What it fixed |
|---|---|---|
| 5 overload-flip | `5c74137` | `FRACUNIT * sin()` bound to `Fixed*int` and truncated sin to 0, **zeroing the whole finesine/finecosine table** (movement, hitscan/puffs, monster sight, render dead at non-cardinal angles). Fill spelled as `double(FRACUNIT)*sin`; `Fixed op <float>` SFINAE-deleted for `* / % << >>`; sibling sites and 4 macros (`PROP_FIXED_PARM`, `GAMEINFOKEY_FIXED`, `AXERANGE`, `ANGLE_TO_FLOAT`) wrapped in `double(...)`; compile-time contract + runtime finesine test. |
| 2 sizeof/width | `2143d24` | finesine wrap tail `memcpy`'d with `sizeof(angle_t)` (4 bytes); after widening it moved half the tail, zeroing finecosine across ~315-360°, so pressing forward near -5° slid you due south. Extracted `zx::FillFineSineWrap` (sizes off the array element type) + `finetable_compute_test`. |
| 2 + 4 | `5f44f0a` | Three more: **savegames** (`arc << fixed_t` bound the QWORD operator and silently doubled every field's width; bumped `SAVEVER`/`MINSAVEVER` past `LAST_FIXED32_SAVEVER` + `static_assert`); **compat slopes** (`P_SetSlope` relied on a 32-bit product wrap — added `zx::Mul32Wrap`); **bot terrain** (`mo->x * finecosine` overflow — rewritten to the `mo->x + lDistance * finecosine` idiom). |
| 3 unsigned-as-signed | `b0666e0` | **View spin**: `R_InterpolateView` multiplied an unsigned `angle_t` delta that zero-extended to +4.2e9 on right turns (view overshot half a circle) — routed through `zx::InterpolateAngleBAM`. **Polyobject rotation**: `RotatePt` aligned with `& 0xFFFFFE00`, a 32-bit mask that wiped the sign on negative offsets, hurling vertices ~65k units — replaced with `zx::AlignDownPow2`. |
| 1 truncation | `94ed7cd` | A `fixed_t` distance from `P_AproxDistance` stored into `int` before reduction — truncated (sign-flipped) past ~32k units. **Point pusher** gave a spurious inward push; **seeker missile** slammed velz to the whole Z gap. Extracted `zx::ComputePusherSpeed` / `ComputeSeekerVelZ`. Found by the `-Wshorten-64-to-32` sweep (638 hits, these two real). |
| 6 signed-shift UB | `66851bd` | Divide-shift computed `(a << shift)` on a signed, possibly-negative `a` — UB. Both the `__int128` path and the 32-bit fast-path now multiply by `(1 << shift)`. Slipped past local ctest because UBSan doesn't fail without `halt_on_error=1`. |
| type hardening | `4923c43` | Made the 32-bit-mask sign-wipe bug (shape 3) impossible to express in the `Fixed` type in the first place. |
| test / value-preservation | `141387b` | Pins `Scale(a,b,1) == raw int64 a*b` so the widened scale layer provably preserves value. |

## The strong-type conversion, in brief

The migration went further than widening: `fixed_t` became `zx::Fixed`, a strong type that makes
whole shapes un-writable. The relevant commits (`4857395` add the type behind a switch → `a21fe09`
default on → `fe1008f` strong is the only mode, plus the codemod passes and hand-fix batches) turned
the implicit narrowings into compile errors you fix deliberately. Net effect for this skill: many
shape-1/3/5 bugs now surface as **build failures**, not silent runtime wrongness — but pre-existing
code paths and anything using `.Raw()` can still hide them, which is why the detection playbook and
runtime `MAP01` check still matter.

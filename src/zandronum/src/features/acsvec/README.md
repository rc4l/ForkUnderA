# features/acsvec

`fua_Vec`, the ACS function zt-bcc's `vec2` and `vec3` compile length, unit, dot and cross to
with `-target fua`, or `-target any` where the engine has it. Modder reference:
[`docs/modding/acs/Vectors.md`](../../../../../docs/modding/acs/Vectors.md).

```
fua_Vec(operands..., int channel, int op)                      24012
```

| op | Operation | Operands | Results |
|---|---|---|---|
| 0 | ABI version (answers 1; the call may be `fua_Vec(0)` alone) | 0 | 1 |
| 1, 2 | length of a vec2, vec3 | 2, 3 | 1 |
| 3, 4 | unit vector of a vec2, vec3 | 2, 3 | 2, 3 |
| 5, 6 | dot product of two vec2s, vec3s | 4, 6 | 1 |
| 7 | cross product of two vec3s | 6 | 3 |

- The call returns the first result and writes the rest to cells 0 onward of the map array in
  map variable `channel` of the module running the call, which is where zt-bcc reads them back.
  An unknown operation or the wrong number of operands returns 0 and writes nothing.
- Every result is the true value rounded half away from zero to 1/65536 and saturated to
  ±0x7FFFFFFF, worked out in 64- and 128-bit integers (`ComputeVec*`), so every machine gives the
  same bits and a client's clientside scripts agree with the server's.
- The numbers are compiled into mods: `ACSF_fua_Vec` and the operations are pinned by a
  `static_assert` and the `AcsVec.TheNumbersNeverMove` test, and zt-bcc's `src/fua_vec.h` holds
  the same ones.
- No netcode: the operations read only their arguments.

## Hooks

- `p_acs.cpp`: `ACSF_fua_Vec` in the function list and its `CallFunction` case, which calls
  `CallVecFunction` with the active module (`activeBehavior`).

## Tests

- `computation/acsvec_compute_test.cpp`, against `computation/acsvec_golden.inc`, which
  `tools/gen_acsvec_golden.py` writes from closed-form exact arithmetic apart from the C++.
- zt-bcc's `acsvm_vec_*_fua` and `_any` CTest runs check the compiler's side against the same
  formulas, and its vector samples ran on this engine as described in the PR.

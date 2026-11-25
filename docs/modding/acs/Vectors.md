# Vector math in ACS

```c
int fua_Vec(operands..., int channel, int op)   // 24012
```

## Availability

> **ZandroX only.** `fua_Vec` is what [zt-bcc](https://github.com/Doruk-Aksoy/bcc_fixed)'s
> built-in `vec2` and `vec3` types compile `Length()`, `Unit()`, `dot` and `cross` to. A mod
> written in BCS never calls it by name: the compiler's `-target` option decides.

| zt-bcc `-target` | What a mod gets |
|---|---|
| `zandronum` (the default) | Plain ACS on every engine, ZandroX included |
| `fua` | `fua_Vec` calls; the mod needs ZandroX |
| `any` | `fua_Vec` on ZandroX, plain ACS on Zandronum 3.3 and older ZandroX builds |

A mod that also runs on stock Zandronum servers builds with `-target any`. It asks the engine for
`fua_Vec` once per map, the first time it needs it, and an engine without it answers 0 and
prints nothing.

## Usage

On ZandroX each operation is one engine call instead of a function of 50 to 440 ACS
instructions, and its result is exact: the true value rounded to the nearest 1/65536 and
saturated at ±32767.99998. The plain-ACS version is within 1/65536 of exact, so the two can
differ in the lowest bit. Every machine running ZandroX gives the same bits, so a client's
clientside scripts agree with the server's.

## Parameters

The operation comes last, then the map variable holding the module's channel array before it;
the operands come first.

| `op` | Operation | Operands | Returns | Writes to the channel |
|---|---|---|---|---|
| 0 | the version, 1 (the call may be `fua_Vec(0)` alone) | none | the version | nothing |
| 1, 2 | length of a vec2, a vec3 | x, y[, z] | the length | nothing |
| 3, 4 | unit vector of a vec2, a vec3 | x, y[, z] | x | y[, z] in cells 0[, 1] |
| 5, 6 | dot product of two vec2s, two vec3s | a's, then b's | the product | nothing |
| 7 | cross product of two vec3s | a's, then b's | x | y, z in cells 0, 1 |

An unknown operation or the wrong number of operands returns 0 and writes nothing. ACC users can
call it through [`fua.acs`](fua.acs), with any map array of at least two cells as the channel.

## Implementation notes

[`src/zandronum/src/features/acsvec/`](../../../src/zandronum/src/features/acsvec/README.md).

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

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`. zt-bcc's [vectors page](https://github.com/Doruk-Aksoy/bcc_fixed/blob/main/doc/vectors.md)
covers the types themselves.

How far an imp is, and whether the player faces it:

```bcs
#library "fuaaim"
#import "zcommon.bcs"

script "Facing" ( int tid ) {
   vec3 to = ActorPos( tid ) - ActorPos( 0 );
   fixed far = to.Length();
   // Within about 45 degrees when the two unit vectors' dot product is over 0.7.
   bool ahead = ( to.Unit() dot ActorDir( 0 ) ) > 0.7;
   Log( s: "imp ", d: far >> 16, s: " units away, ahead ", d: ahead );
}
```

<!-- example
pukename Facing 31 => /imp 816 units away, ahead 0/
pukename Place 400 680 0
pukename Facing 31 => /imp 200 units away, ahead 1/
-->

Pulling the player toward a point at a steady speed, a step a tic:

```bcs
#library "fuapull"
#import "zcommon.bcs"

script "PullToward" ( int tid ) {
   vec3 to = ActorPos( tid ) - ActorPos( 0 );
   if ( to.Length() > 64.0 ) {
      vec3 push = to.Unit() * 12.0;
      SetActorVelocity( 0, push.x, push.y, push.z, false, false );
      Log( s: "pulling at ", d: push.Length() >> 16 );
   }
}
```

<!-- example
pukename PullToward 31 => /pulling at 1[12]/
-->

## Best practices

- **Build with `-target any`** when the mod also runs on stock Zandronum: ZandroX then answers in one
  engine call, and stock servers run the plain ACS.
- **Whole units are `x >> 16`.** Outside a `strict namespace`, as in these examples and every ACC-style
  file, a `fixed` is a plain number: `( int ) x` keeps its bits, so `( int ) 816.5` is 53,510,144, and
  `int( x )` doesn't compile. Shift right by 16 for the whole part, or print it with `f:`. Inside a
  `strict namespace`, `int( x )` converts; zt-bcc's
  [strict mode page](https://github.com/Doruk-Aksoy/bcc_fixed/blob/main/doc/strict-mode.md) lists the
  other differences, such as `fixed * fixed` being an integer multiply outside it (use `FixedMul`).
- **Take `Unit()` once** and keep it when several uses need the same direction; each call is a square
  root.
- **Compare squared lengths** (`v dot v`) against a squared distance when you only need "nearer than":
  it skips the square root, and both sides stay within fixed point's range up to about 181 units' length.

## Online

Every machine running ZandroX computes the same bits for each operation, so a clientside script's
vectors match the server's for the same inputs.

## Implementation notes

[`src/zandronum/src/features/acsvec/`](../../../src/zandronum/src/features/acsvec/README.md).

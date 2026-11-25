# TRNSLATE

## Availability

> **ZandroX only** among Zandronum versions; ported from UZDoom, so a GZDoom/UZDoom `TRNSLATE` lump loads as written.
>
> **Works in netgames.** Every machine reads the same lumps, and `A_SetTranslation` is decided by the
> server and sent to the clients.

## Usage

A `TRNSLATE` lump defines colour translations once, by name. DECORATE's `Translation` property and
`A_SetTranslation` can then use the name instead of repeating the ranges in each actor. Every
`TRNSLATE` lump is read, in load order. A name defined again replaces the earlier definition.

```
Name [: Base] = "entry" [, "entry" ...]
```

- `Name`: the translation's name. Names ignore case.
- `Base` (optional): what the table starts from. It is a translation defined earlier, `Ice`, `None`,
  `Player1` to `Player8`, or a standard translation `0` to `6` (the number DECORATE's
  `Translation 1` takes). Without a base, the table starts unchanged.
- `entry`: a range in the syntax DECORATE's `Translation` takes (`"112:127=192:207"`,
  `"0:255=[255,0,0]:[0,0,0]"`, `"16:31=%[0,0,0]:[1,1,1]"`). It can instead name a palette lump, which
  may be followed by the index its colours start at: `"MYPAL", 16`. A palette lump can be a PNG (its
  `PLTE`), a JASC-PAL text, or a raw 768-byte palette.

A syntax error, or a base that is not defined, stops the game from loading, as it does in UZDoom.
A malformed range is printed and skipped.

### Using it

- `Translation "Name"`: the actor's default translation.
- `A_SetTranslation (name transname)` (**Actor**): sets the actor's translation by name. `""` restores
  its class's translation, and so does `"None"`. A name that is not defined is ignored without a message.

### Clearing a translation

> **Differs from UZDoom.** In UZDoom, `A_SetTranslation("None")` removes the actor's translation. Here it
> restores the class's translation, the same as `""`.

ZandroX's DECORATE parser passes an empty string to an action as the name `None`. By the time
`A_SetTranslation` runs, `""` and `"None"` are the same value, so it cannot tell them apart.
`Thing_SetTranslation(tid, 0)` also restores the class's translation rather than clearing it.

To show an actor without its class's translation, define a translation that changes nothing and switch
to it:

```
// TRNSLATE
Untranslated = "1:1=1:1"
```

```
	BOSS H 0 A_SetTranslation("Untranslated")
```

The entry must not touch palette index 0. A range that includes index 0 makes that colour transparent,
which an actor with no translation does not do.

## Examples

```
// TRNSLATE
BlueBaron = "112:127=192:207", "16:31=200:207"
DarkBaron : BlueBaron = "0:255=%[0,0,0]:[0.5,0.5,1]"
```

```
actor BlueBaron : BaronOfHell replaces BaronOfHell
{
	Translation "BlueBaron"
	States
	{
	Pain:
		BOSS H 2 A_SetTranslation("DarkBaron")
		BOSS H 2 A_Pain
		BOSS H 0 A_SetTranslation("")
		Goto See
	}
}
```

Not supported: `RainPillar1` to `RainPillar8` as bases, which have no table here.

## Implementation notes

[`features/trnslate`](../../../src/zandronum/src/features/trnslate/README.md)

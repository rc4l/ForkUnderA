# mkiwad

Builds an IWAD from a manifest. Every byte is generated; nothing is copied from another file.

```
python tools/mkiwad/mkiwad.py tools/mkiwad/fuamega.json
```

## Why this exists

Some total conversions are whole games and ship no base file you can legally redistribute. The
engine still needs one: it identifies the game from the IWAD, and refuses to start without it.
`fuamega.wad` is that base for Mega Man 8-bit Deathmatch, so the catalogue entry is hostable out
of the box.

## What an IWAD of this kind holds

| Part | Count | Note |
|---|---|---|
| One pixel patches | 472 | Stock texture names resolve instead of erroring |
| Blanked chrome | 24 | Mugshot, menu bits, intermission words |
| Drawn chrome | 14 | `M_PAUSE`, digits, `%`, `/`, `-` |
| Border tiles | 8 | |
| `MAP01` | 1 | One square sector, one player start |
| `ENDOOM`, sky, markers | 6 | |

The pk3 supplies `PLAYPAL` and `COLORMAP`, so this file has neither.

## Generators

`text`, `border`, `sky`, `endoom`, `map`, `blank`, `stub`, `marker`. A manifest entry names one and
passes its arguments. Add a generator here, not a special case in a manifest.

`blank` and `stub` are both one pixel and are not interchangeable. `stub` draws a pixel and exists
so a texture name resolves. `blank` draws nothing and exists to switch a piece of engine chrome
off.

## Why so much is blank

Measured, not assumed. In MM8BDM's own base file the mugshot, `M_SKILL` and every intermission
word are 1x1: the pk3 draws its own HUD and intermission, so those slots are deliberately empty.
Drawing art for them puts a stranger on screen. `fuamega.json` mirrors that classification, and the
lump order too, so the namespace layout matches a file known to work.

## Identification

`GetIWadInfo` returns the **first** `IWad` block whose `MustContain` is fully satisfied
(`d_iwad.cpp`). Doom 2's block requires only `MAP01`, so any file with a map matches it. The
`FUAMEGA` marker lump plus a block placed above Doom 2's in `iwadinfo.txt` is what keeps this file
identified as itself.

## After changing the manifest

Rebuild, then update the `fuamega.wad` line in `config/iwadallowlist.txt` with the new sha256. The
packagers copy the built wad and fail closed if it is missing.

## Interchangeable with the real base file

Verified both directions, live: a client on `fuamega.wad` joins a server on `megagame.wad`, and a
client on `megagame.wad` joins a server on `fuamega.wad`.

The join check is not a hash of the IWAD file. `SERVER_PerformAuthenticationChecksum` compares an
MD5 of **the current map's lumps** (`sv_main.cpp`), and in a total conversion those come from the
pk3, which both sides have identically. The base file supplies chrome nobody plays on.

## Preference

`config/iwadsubstitutes.txt` maps `megagame.wad` to `fuamega.wad`, and `PickIwad` prefers the real
file whenever it is present. Owning it settles it; ours is the fallback.

## Licence

GPL-3.0-or-later, stated in the wad's own `COPYING` lump as well as here.

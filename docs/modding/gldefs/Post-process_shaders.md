# Post-process shaders in GLDEFS

```
HardwareShader PostProcess <beforebloom | scene | screen>
{
	Name "<name>"
	Shader "<lump>" <330..450>
	Uniform <int | float | vec2 | vec3 | vec4> <name> [= <values> | = cvar <cvar>]
	Texture <name> "<texture>"
	Enabled
}
```

## Availability

> **New in ZandroX; UZDoom's syntax.** Zandronum has no post-process shaders. ZandroX reads UZDoom's
> `HardwareShader PostProcess` blocks as UZDoom writes them, so a block written for UZDoom works here, and
> the console commands are UZDoom's too.
>
> They draw on the Vulkan and WebGPU renderers. GL Legacy parses them and draws without them, and says so
> once at launch.

## Usage

Each block is a full-screen fragment shader run over the picture. Every shader that is on runs, in GLDEFS
order within its stage. Turn them on and off with [`fua_SetShaderEnabled`](../acs/Post-process_shaders.md)
from ACS, or `shaderenable` from the console.

### Stages

There is no bloom, so the stages run in this order:

1. `beforebloom`: the 3D view without the weapon.
2. The weapon and the screen's tint (pain, pickups) are drawn.
3. `scene`: the 3D view with the weapon.
4. The HUD, the menus and the console are drawn.
5. `screen`: the whole screen, HUD and all.

`beforebloom` and `scene` run over the 3D view at the size dynamic resolution draws it, so they get
cheaper when the scale drops.

### The block

- `Name`: what ACS and the console call it. ACS matches it in any case; the console commands match it
  exactly.
- `Shader`: the lump holding the program, and a GLSL version from 330 to 450. Any `#version` line in the
  lump is dropped.
- `Uniform`: a value the program reads by that name.
  - Give `= <values>` as one number per component, separated by spaces (`= 1.0 0.4 0.1` for a `vec3`).
    Without one, every component starts at 0 except a `vec4`'s w, which starts at 1.
  - `= cvar <name>` reads that cvar each frame instead. An `int` or `float` cvar fits an `int` or `float`
    uniform, and a `color` cvar fits a `vec3` or `vec4`; anything else is an error. A cvar that doesn't
    exist is a warning, and the uniform starts at 0.
  - The block needs a `Name` before any cvar uniform.
- `Texture`: a picture the program reads by that name, given by its texture name (`"STARTAN2"`). One the
  game doesn't have reads white, with a note in the console.
- `Enabled`: the shader starts on. Without it, it starts off. Every level starts from this.

An unknown keyword or stage, an unknown uniform type, or a version outside 330–450 stops GLDEFS loading
with an error, as on UZDoom.

### The program

Write `void main()`. The engine declares everything else, so don't declare these yourself:

| Name | What it is |
|---|---|
| `TexCoord` | This pixel's place on the picture, 0 to 1, with y up as on GL. |
| `InputTexture` | The picture so far: the stage's input, or the previous shader's result. Read it with `texture(InputTexture, TexCoord)`. |
| `LastInputTexture` | The previous frame's 3D view, as the `beforebloom` and `scene` stages left it. It is kept only when some shader's text mentions it. |
| your `Uniform`s | By the names you gave them. |
| your `Texture`s | By the names you gave them, read with `texture(name, uv)`, the first row at y 0, the bottom. |
| `InputTime` | Real seconds, from the engine's clock. |
| `InputTimeDelta` | Real seconds since the previous frame. |
| `InputTimeGame` | Game seconds in this level. It stops when the game pauses. |
| `fua_EnabledTime` | Game seconds since this shader went on. **ZandroX only.** |
| `FragColor` | What you write: this pixel's colour. |

`gl_FragCoord` and `textureSize(InputTexture, 0)` count the input's pixels, y up, whatever size the pass
draws at. `texelFetch`, `textureLod` and `textureOffset` work on `InputTexture` and `LastInputTexture` too.

### Precision

Everything a shader reads and writes is 16-bit float, as on UZDoom:
- additive light past 1 and subtractive light below 0 reach the shader instead of being clipped;
- a chain of shaders hands those values on;
- a frame fed back through `LastInputTexture` fades all the way to black.

What the screen finally shows is clipped to 0–1.

### Cost

- **None declared:** nothing.
- **Declared but off:** a target of its own for the 3D view and two frame-sized pictures, plus one more
  if any shader reads `LastInputTexture` (15.5 MB each at 1920×1080). They are made at load, so turning a shader on in play stutters nothing.
- **On:** one full-screen pass each, plus a copy per stage. Three shaders at 1080p cost about 0.25 ms of a
  desktop GPU's time.

### Console

| Command | Does |
|---|---|
| `shaderenable <name> <1 \| 0 \| -1>` | Turns one on, off, or toggles it, for everyone, until the level ends. |
| `shaderuniform <name> <uniform> <values...>` | Sets a uniform for everyone. |
| `listshaders` / `listuniforms <name>` | What GLDEFS declared. |
| `fua_postshaders` | Each shader as this machine draws it now: on or off, since which tic, and every uniform's value. |
| `gl_custompost 0` | Turns them all off, for players who don't want them. |

## Examples

GLDEFS:

```
HardwareShader PostProcess scene
{
	Name "heatwave"
	Shader "shaders/heatwave.fp" 330
	Uniform float strength = 0.004
}
```

`shaders/heatwave.fp`, a shimmer that grows over the first two seconds after it is switched on:

```glsl
void main()
{
	float ramp = min(fua_EnabledTime / 2.0, 1.0);
	vec2 uv = TexCoord;
	uv.x += sin(uv.y * 60.0 + InputTimeGame * 8.0) * strength * ramp;
	FragColor = texture(InputTexture, uv);
}
```

It starts off. ACS turns it on, for everyone or for one player:

```c
fua_SetShaderEnabled("heatwave", true);
```

## Implementation notes

[`features/postprocess`](../../../src/zandronum/src/features/postprocess/README.md).

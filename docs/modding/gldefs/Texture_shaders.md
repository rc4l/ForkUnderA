# Texture shaders in GLDEFS

```
HardwareShader [Texture | Flat | Sprite] <name>
{
	Shader "<lump>"
	Speed <float>
	fua_ProcessLighting <zandronum | uzdoom>
}
```

## Availability

> `Shader` and `Speed` are Zandronum's. **`fua_ProcessLighting` is ZandroX only**; another port stops
> loading GLDEFS at it.
>
> These shaders draw on GL and Vulkan alike.

## Usage

A texture shader replaces how a texture, flat or sprite is coloured. Its lump defines one of these:

| Entry | Written for | What the engine does with it |
|---|---|---|
| `vec4 ProcessTexel()` | GZDoom and UZDoom | Returns the surface's colour, and the engine lights it afterwards like any texture. |
| `vec4 Process(vec4 color)` | Zandronum 3.3 and GZDoom 1.x | Depends on `fua_ProcessLighting`, below. |

A shader that defines UZDoom's `SetupMaterial` or `ProcessMaterial` is not supported yet. The console names
it at launch, and its textures draw without it.

### `fua_ProcessLighting`

Zandronum 3.3 and UZDoom both accept `Process(vec4 color)`, but they light it differently:

- **`zandronum`** (the default): `color.rgb` is the light at this pixel, made up of the sector's light, wall
  glow and dynamic lights; `color.a` is the surface's alpha. The engine does not light the result again;
  it only adds coloured fog, and the colours of a fixed colormap such as invulnerability. So the shader
  chooses what is lit: `getTexel(uv) * color` darkens with the room, and anything added without `color`
  glows in the dark.
- **`uzdoom`**: `color` is white, and the engine lights the result afterwards, so everything the shader
  returns darkens with the room.

A shader made for Zandronum 3.3 needs nothing. Give `uzdoom` to a shader that was tuned on UZDoom and
returns colour without multiplying by `color`.

`fua_ProcessLighting` has no effect on a `ProcessTexel` shader or on HUD images. A value other than the two
above is noted in the console, and the default is kept.

## Examples

GLDEFS:

```
HardwareShader Texture "LAVA1"
{
	Shader "shaders/lava.fp"
	Speed 1.0
}
```

`shaders/lava.fp`, crust lit by the room with glowing cracks that stay bright in the dark:

```glsl
uniform float timer;

vec4 Process(vec4 color)
{
	vec4 crust = getTexel(gl_TexCoord[0].st);
	float crack = smoothstep(0.6, 0.9, crust.r) * (0.75 + 0.25 * sin(timer * 3.0));
	return vec4(crust.rgb * color.rgb + vec3(1.0, 0.45, 0.1) * crack, crust.a * color.a);
}
```

## Implementation notes

[`features/hwshaders`](../../../src/zandronum/src/features/hwshaders/README.md).

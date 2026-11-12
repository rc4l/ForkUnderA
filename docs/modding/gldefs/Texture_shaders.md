# Texture shaders in GLDEFS

```
HardwareShader [Texture | Flat | Sprite] <name>
{
	Shader "<lump>"
	Speed <float>
	Texture <name> "<texture>"
	Define <NAME> [= "<value>"]
	DisableAlphaTest
	Material <normal | specular | pbr | ...>
	fua_ProcessLighting <zandronum | uzdoom>
	fua_Name "<set>"
	fua_Uniform <type> <name> [= <values> | = cvar <cvar>]
}

Material [Texture | Flat | Sprite] <name>
{
	Brightmap <texture>
	Normal <texture>
	Specular <texture>
	Metallic <texture>
	Roughness <texture>
	AO <texture>
	Glossiness <float>
	SpecularLevel <float>
	Shader "<lump>"
	Speed, Texture, Define, DisableAlphaTest, DisableFullbright, NoMipmap, ThisWad, IWad
}
```

## Availability

> `Shader` and `Speed` are Zandronum's. `Texture`, `Define`, `DisableAlphaTest`, `Material` and the
> `Material` block are UZDoom's. **`fua_ProcessLighting`, `fua_Name` and `fua_Uniform` are ZandroX only**.
>
> Zandronum 3.3's `Process` and GZDoom's `ProcessTexel` draw on GL and Vulkan. Everything else UZDoom
> added (`SetupMaterial`, `ProcessMaterial`, `GetTexCoord`, `Texture` lines, `Material` blocks and their
> light models) and `fua_Uniform` draw on Vulkan only; GL draws those textures without their shader.

## Usage

A texture shader replaces how a texture, flat or sprite is coloured. Its lump defines one of these, and
the first found, in this order, is the one called:

| Entry | Written for | What the engine does with it |
|---|---|---|
| `void SetupMaterial(inout Material material)` | UZDoom | Fills the material (colour, normal, bright, glow, the type's maps); the engine lights it. |
| `Material ProcessMaterial()` | GZDoom 3.x | Returns the material; lit without its glow or bright layer, as UZDoom does. |
| `vec2 GetTexCoord()` | UZDoom | Where the texture and its maps are read; the engine fills the material there. |
| `vec4 ProcessTexel()` | GZDoom and UZDoom | Returns the surface's colour, and the engine lights it afterwards like any texture. |
| `vec4 Process(vec4 color)` | Zandronum 3.3 and GZDoom 1.x | Depends on `fua_ProcessLighting`, below. |

A shader may also define UZDoom's `ProcessLight(Material, vec4)` or the older `ProcessLight(vec4)`,
which receives the light before dynamic lights are added.

The shader reads UZDoom's names: `getTexel`, `desaturate`, `SetMaterialProps`, `ApplyNormalMap`,
`vTexCoord`, `timer`, `pixelpos`, `vWorldNormal`, `uCameraPos`, `vColor`, `uSpecularMaterial`, and the
samplers `tex`, `texture2` onward, `brighttexture`, `normaltexture`, `speculartexture`,
`metallictexture`, `roughnesstexture`, `aotexture` and each `Texture` line's name. `texture`,
`texture2D`, `textureSize` and `texelFetch` all work on them.

### `Material` blocks

A `Material` block gives a texture its maps, as UZDoom's does. The maps decide its light model:
- **Normal and specular:** Blinn-Phong highlights from dynamic lights, shaped by `Glossiness` (default 10)
  and `SpecularLevel` (default 0.1).
- **Normal, metallic, roughness and AO:** physically based light.
- **Anything less:** the usual light. `Brightmap` works like a GLDEFS `brightmap` block.

A shader given in the block is of the type its maps give. A `HardwareShader` on a texture whose maps give
another type than its `Material` line is not used, and the texture draws with the built-in shader for its
maps, as on UZDoom. Maps need a GPU with bindless textures; without one, a texture keeps the usual light.

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

### Uniforms scripts set

```
HardwareShader Texture "LAVA1"
{
	Shader "shaders/lava.fp"
	fua_Name "lava"
	fua_Uniform float heat = 1.0
	fua_Uniform vec3 tint = cvar my_lava_tint
}
```

**ZandroX only, Vulkan only.** `fua_Name` names a set of uniforms, and `fua_Uniform` declares one, with the
syntax of a [post-process shader's `Uniform`](Post-process_shaders.md). ACS sets them with
[`fua_SetShaderUniform*`](../acs/Post-process_shaders.md), by the set's name, for everyone or for one
player, until the level ends; savegames and netgames keep them.

- Every block giving the same `fua_Name` shares its uniforms, so ten lava textures heat up together. A
  block may name a set without declaring anything, and reads the uniforms other blocks declared.
- The shader reads each uniform by its name. Declaring it in the shader too (`uniform float heat;`) is
  allowed and ignored.
- `fua_Uniform` comes after `fua_Name` in its block. A name declared again with another type is an error.
- All texture shaders share 64 uniforms between them.
- GL draws such a shader's textures without it.

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

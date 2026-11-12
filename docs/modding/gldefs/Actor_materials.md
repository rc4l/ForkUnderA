# Actor materials

```
// GLDEFS
fua_Material "<name>"
{
	Shader "<lump>"
	Define <NAME> [= "<value>"]
	fua_ProcessLighting <zandronum | uzdoom>
	fua_Uniform <type> <name> [= <values>]
}

// DECORATE, in an actor
fua_Material "<name>"

// ACS, after #include "fua.acs"
int fua_SetActorMaterial(int tid, str material)
int fua_SetActorUniform1f(int tid, str uniform, fixed x)
int fua_SetActorUniform2f(int tid, str uniform, fixed x, fixed y)
int fua_SetActorUniform3f(int tid, str uniform, fixed x, fixed y, fixed z)
int fua_SetActorUniform4f(int tid, str uniform, fixed x, fixed y, fixed z, fixed w)
int fua_SetActorUniform1i(int tid, str uniform, int x)
```

## Availability

> **ZandroX only, Vulkan only.** No other port has per-actor materials. GL draws such actors as usual.

## Usage

A material is a [texture shader](Texture_shaders.md) that belongs to actors instead of a texture: an
actor with one draws its sprite or model through it, whatever frame it shows. Its uniforms are each
actor's own, so two imps of one class can glow differently.

- **DECORATE** gives a class its starting material. `fua_Material "none"` gives none.
- **`fua_SetActorMaterial`** gives the actors with that tid (0 the script's activator) a material, or
  takes it away with `""` or `"none"`. A new material starts from its defaults.
- **`fua_SetActorUniform*`** sets one uniform on each of those actors whose material declares it. The
  suffix says how many numbers you pass, as for [post-process shaders](../acs/Post-process_shaders.md).
- **Returns** how many actors it changed or set; 0 for a material or uniform GLDEFS does not declare, or
  too few arguments.
- **Lasts** as long as the actor. A savegame keeps it, a joining player gets it, and a new level starts
  every actor from DECORATE.

Limits: 8 uniforms a material, 256 materials, and 4096 actors holding values of their own at once (a
script setting one more is ignored). A uniform's shader reads it by its name; an `int` reads whole numbers.

## Examples

GLDEFS:

```
fua_Material "rage"
{
	Shader "shaders/rage.fp"
	fua_Uniform vec3 tint = 1.0 1.0 1.0
	fua_Uniform float pulse = 0.0
}
```

`shaders/rage.fp`:

```glsl
vec4 ProcessTexel()
{
	vec4 t = getTexel(vTexCoord.st);
	float p = 0.5 + 0.5 * sin(timer * 8.0);
	return vec4(mix(t.rgb, t.rgb * tint, pulse * p), t.a);
}
```

DECORATE: `fua_Material "rage"` in the imp's actor. ACS, when one imp enrages:

```c
fua_SetActorUniform3f(tid, "tint", 1.0, 0.2, 0.2);
fua_SetActorUniform1f(tid, "pulse", 1.0);
```

## Implementation notes

[`features/actormaterials`](../../../src/zandronum/src/features/actormaterials/README.md).

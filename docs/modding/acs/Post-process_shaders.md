# Post-process shaders in ACS

```c
int fua_SetShaderEnabled(str shader, bool enable [, bool activatorOnly [, int durationTics]])
int fua_SetShaderUniform1f(str shader, str uniform, fixed x [, bool activatorOnly])
int fua_SetShaderUniform2f(str shader, str uniform, fixed x, fixed y [, bool activatorOnly])
int fua_SetShaderUniform3f(str shader, str uniform, fixed x, fixed y, fixed z [, bool activatorOnly])
int fua_SetShaderUniform4f(str shader, str uniform, fixed x, fixed y, fixed z, fixed w [, bool activatorOnly])
int fua_SetShaderUniform1i(str shader, str uniform, int x [, bool activatorOnly])
```

## Availability

> **ZandroX only.** These do what UZDoom's ZScript `PPShader.SetEnabled` and `PPShader.SetUniform*`
> do, from ACS. They control the post-process shaders a mod declares in GLDEFS
> ([`HardwareShader PostProcess`](../gldefs/Post-process_shaders.md), UZDoom's syntax).
>
> Post-process shaders draw on the Vulkan and WebGPU renderers only. On GL Legacy the calls still
> work and are still sent, but nothing is drawn.

They are not in stock `acc`. Include [`fua.acs`](fua.acs) after `zcommon.acs`:

```c
#include "zcommon.acs"
#include "fua.acs"
```

## Usage

`fua_SetShaderEnabled` turns a shader on or off. `fua_SetShaderUniform*` sets one of its uniforms.

- **For everyone, or only the activator.** With `activatorOnly` false (the default) the change applies
  to every player. With it true, only the player who activated the script sees it: fire on one
  player's screen, say.
  - A monster has no screen, so an activator-only call it activates is refused.
  - A script with no activator at all (an `OPEN` script) is refused on a server. In single player, and in
    a `CLIENTSIDE` script, it applies to the local player.
- **A player's own setting wins.** For each player, what an activator-only call set for them comes first,
  then what was set for everyone, then GLDEFS. This holds for the shader being on and for each uniform:
  turning one off for a single player hides it from them while it stays on for everyone else. An own
  setting lasts like any other: only a timed enable running out drops it.
- **Timed.** With `durationTics` above 0, an enable is undone after that many tics. One player's own
  falls back to everyone's; everyone's falls back to GLDEFS (`Enabled` or not). Each player's machine
  times it, so nothing more is sent while it runs. A disable is never timed.
- **Enabling again refreshes it.** Calling `fua_SetShaderEnabled` on a shader that is already on
  restarts a timed one's countdown from that moment, with the new duration, and gives an untimed call
  an untimed shader. Its clock keeps running, so `fua_EnabledTime` does not restart.
- **Until the level ends, or the round.** Every level starts from GLDEFS (`Enabled` or not), and so
  does every round in a game mode that resets the map (survival, LMS and the like). A savegame keeps
  the changes.
- **Names are checked.** A shader that GLDEFS does not declare, or a uniform its block does not declare,
  is ignored. Names match GLDEFS's `Name` in any case, and uniforms exactly.
- **Returns** 1 when the shader now stands as asked, whether or not it changed. It returns 0 when the call
  was refused: an undeclared name, a string that isn't one, too few arguments, or an activator-only call
  with no player to apply it to.

Uniforms:

- The suffix says how many numbers you pass. The shader's declaration says what it reads: setting a
  `vec4` with `2f` leaves z at 0 and w at 1, and numbers past what the uniform holds are dropped.
- The `f` functions take fixed point, so write `0.5`, not `1/2`. `1i` takes a plain integer.
- An `int` uniform set with an `f` function takes the whole part (`2.7` reads 2); a `float` set with `1i`
  takes the integer as a float.
- A uniform the level never set reads its cvar, if GLDEFS gave it one, else its GLDEFS default.
- The shader's built-in times need no ACS. `fua_EnabledTime` is game seconds since it went on, for
  effects that start when they are switched on. [The GLDEFS page](../gldefs/Post-process_shaders.md)
  lists everything a shader reads.

While testing, the console's `fua_postshaders` prints every shader as your machine draws it: on or off,
since which tic, and each uniform's value.

## Parameters

- `shader`: the `Name` from its GLDEFS block.
- `enable`: on (true) or off (false).
- `activatorOnly`: true for the activator's screen only. Default false.
- `durationTics`: tics until an enable is undone; 0 (the default) never. Ignored when disabling.
- `uniform`: the uniform's name, as its GLDEFS `Uniform` line declares it.
- `x`, `y`, `z`, `w`: the values.

## Online

A server sends each change to the clients it reaches: all of them, or the activator's alone. A player
joining later gets everyone's changes, with the clocks of the shaders already on, and none of another
player's. A call that changes nothing, such as the same value set every tic, sends nothing.

A `CLIENTSIDE` script runs on each client, so its changes stay on that machine. Use one when every
client should decide for itself. An `ENTER` script marked `CLIENTSIDE` with `activatorOnly` true changes
only the entering player's own screen.

## Examples

GLDEFS:

```
HardwareShader PostProcess scene
{
	Name "burning"
	Shader "shaders/burning.fp" 330
	Uniform float strength = 1.0
	Uniform vec3 tint = 1.0 0.4 0.1
}
```

A pickup that sets its player's screen burning for five seconds, darker red, and leaves everyone
else's alone:

```c
script "SetOnFire" (void)
{
	fua_SetShaderUniform3f("burning", "tint", 0.8, 0.1, 0.05, true);
	fua_SetShaderEnabled("burning", true, true, 5 * 35);
}
```

A switch that turns a shader on for everyone and ramps it up:

```c
script "PowerOn" (void)
{
	fua_SetShaderEnabled("burning", true);
	for (int i = 0; i <= 20; i++)
	{
		fua_SetShaderUniform1f("burning", "strength", i * 0.05);
		Delay(2);
	}
}
```

Each `SetUniform` call in a netgame is one small message to the clients, so a ramp like this one costs
21 messages. For a ramp every client can work out, read `fua_EnabledTime` in the shader instead.

`shaders/burning.fp`, which the uniforms above feed:

```glsl
void main()
{
	vec3 c = texture(InputTexture, TexCoord).rgb;
	float flicker = 0.85 + 0.15 * sin(fua_EnabledTime * 9.0);
	FragColor = vec4(mix(c, c * tint * 2.0, strength * flicker), 1.0);
}
```

## Implementation notes

[`features/acs-shaders`](../../../src/zandronum/src/features/acs-shaders/README.md) and
[`features/postprocess`](../../../src/zandronum/src/features/postprocess/README.md).

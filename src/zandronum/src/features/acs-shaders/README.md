# features/acs-shaders

FUA ACS functions that control GLDEFS post-process shaders (`features/postprocess`), the way UZDoom's
ZScript `PPShader` does. A change can apply to everyone or only to the script's activator, and an enable
can be timed. Modder reference: [`docs/modding/acs/Post-process_shaders.md`](../../../../../docs/modding/acs/Post-process_shaders.md).

```
fua_SetShaderEnabled(str shader, bool enable [, bool activatorOnly [, int durationTics]])   24000
fua_SetShaderUniform1f(str shader, str uniform, fixed x [, bool activatorOnly])           24001
fua_SetShaderUniform2f / 3f / 4f(..., fixed x, y [, z [, w]] [, bool activatorOnly])       24002-24004
fua_SetShaderUniform1i(str shader, str uniform, int x [, bool activatorOnly])             24005
```

- The numbers are a block no upstream uses (24000-24099). Mods compile them into their bytecode, so
  they never move.
- Each returns 1 when the state now holds what was asked, set again or not, and 0 when the call was
  refused.
  - A shader GLDEFS does not declare, or a uniform its declaration lacks, is refused, so no script fills
    the level's state with names (`ComputeShaderCallDeclared`).
  - So are a bad string, too few arguments, and an activator-only call whose activator is not a player.
- A change lasts until the level ends or a round resets (`GAME_ResetMap`). A savegame keeps it.
- Enabling a shader that is already on keeps its clock (`fua_EnabledTime`). A timed enable restarts its
  countdown from the call, with the new duration. A disable is never timed.

## Who a change reaches

`ComputeShaderRoute`:

| where the script runs | everyone | activator only |
|---|---|---|
| offline | applied | applied when the activator is the console player, or there is none |
| server | recorded, sent to every client | recorded for that player, sent to their client only (`SVCF_ONLYTHISCLIENT`) |
| client (a `CLIENTSIDE` script) | applied on this client | applied when the activator is this client's player, or there is none |

A server records every change in `FLevelLocals::fua_postShaders`, as everyone else does. That is what a
joining player and a savegame get.

A dedicated server has no renderer to read GLDEFS. Its null renderer reads only the post-process
blocks (`ReadPostProcessDeclarations`, from `ComputePostProcessDeclarations`), so it refuses the same
names a client would. `fua_postshaders_reread` does the same on any build, for tests.

## Online

- `SVC2_FUASETSHADERENABLED` carries:
  - the shader's name;
  - whether it is the receiver's own;
  - on or off;
  - the tics left (0 for none);
  - the tics it has already run, so a joiner's clock agrees with everyone's.

  Each client times a timed effect itself, so it costs one message.
- `SVC2_FUASETSHADERUNIFORM` carries the shader's and uniform's names, whether it is the receiver's own,
  and the value: an int, or 1 to 4 floats.
- Only a real change is sent. A script setting the same value every tic sends nothing after the first
  (`ComputeSetShaderEnabled`, `ComputeSetShaderUniform`).
- Names travel as strings, checked on both ends.
- Joins and leaves:
  - A joining player gets everyone's entries and their own, at the end of `SERVER_SendFullUpdate`
    (`ComputeJoinEntries`). A timed entry that has run out sends only its uniforms.
  - A leaving player's own entries are dropped (`ComputeDropPlayer`).
- A round reset clears the state on the server and on each client, which run their own reset.
- Both commands are appended after the last existing ones, so no id moves. `tools/data/protocol-snapshot.txt`
  records them, and `acsshaders_wire_nettest.cpp` pins their bytes.

`fua_postshaders` prints the state's entry count and the tic, then each shader as this machine draws it:
on or off and since which tic, and every uniform's value. Netplay scenarios read it on each client.

## Savegames

`SAVEVER` 4526: `SerializeLevelShaders` stores the table after the sounds. A save from 4525 or earlier
loads with none.

## Tests

- `scenarios acsshaders` and `acsshadersopt` cover, single-player and in both SPIR-V optimizer modes:
  - every uniform kind;
  - a player's own value over everyone's;
  - a timed enable, and one enabled again before it ends;
  - a save, a new level;
  - a LOADACS library whose tagged strings name undeclared shaders and get 0 back.
- `scenarios netshaders`:
  - **loopback** (its server reading GLDEFS as a dedicated one does):
    - every route, and a server's netcap showing that values set again and undeclared names send
      nothing;
    - a CLIENTSIDE call with undeclared names leaving its client's state alone;
    - timing and a refreshed timer;
    - a late joiner's state and clock;
    - a reused slot, a map change.
  - **lossy:** the same at 100 ms and 5% loss.
  - **roundreset:** a survival round reset clearing the shaders and the music volume, for players
    and a joiner.

## Files

- `acsshaders.{h,cpp}`: the ACS call, what a client does with a server's command, the join, leave and
  round-reset handling, and the save.
- `computation/acsshaders_compute`: the numbers, the arguments, the route, the timing, and the join and
  leave entries.

## In-engine hooks

- `p_acs.cpp`: `ACSF_fua_SetShaderEnabled` to `ACSF_fua_SetShaderUniform1i`, and their cases.
- `protocolspec/spec.misc.txt`, `network_enums.h`, `sv_commands.{h,cpp}`, `cl_main.cpp`: the two commands.
- `sv_main.cpp`: `SendShaderStateTo` at the end of `SERVER_SendFullUpdate`, and `DropPlayerShaders` in
  `SERVER_DisconnectClient`.
- `g_game.cpp` `GAME_ResetMap`: `ResetLevelShaders`, and the music volume back to 1.
- `g_level.cpp`: `SerializeLevelShaders` beside `P_SerializeSounds`. `version.h`: `SAVEVER` 4526.
- `r_nullrenderer.h` `Init`: `ReadPostProcessDeclarations`.
- `features/postprocess`: a uniform's `kind`, `PostShaderDeclared`, `ReadPostProcessDeclarations`, and the
  `fua_postshaders` and `fua_postshaders_reread` commands.
- `features/gldefs/gldefs.cpp`: `gl_GameDefsLump`, the game's own `*DEFS` lump, which `gl_ParseDefs` and
  `r_nullrenderer.h` hand that reader.
- `features/acs-setmusicvolume/musicvolume_wire_nettest.cpp`: no longer assumes its command is last.
- `tools/boundary-allow.json`: `acsshaders.cpp`'s four sends.
- `src/CMakeLists.txt`: `acsshaders.cpp` in the engine's sources.

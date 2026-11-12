# features/actormaterials

Per-actor GLDEFS materials: a shader an actor's sprite or model draws with over its texture's own, with
uniforms each actor holds for itself. DECORATE gives a class its material, and ACS changes one actor's
material or uniforms while playing. Vulkan only. Modder reference:
[`docs/modding/gldefs/Actor_materials.md`](../../../../../docs/modding/gldefs/Actor_materials.md).

```
fua_Material "<name>"
{
	Shader "<lump>"
	Define <NAME> [= "<value>"]
	fua_ProcessLighting <zandronum | uzdoom>
	fua_Uniform <type> <name> [= <values>]
}
```

```
fua_SetActorMaterial(int tid, str material)                  24006
fua_SetActorUniform1f..4f(int tid, str uniform, fixed x...)   24007-24010
fua_SetActorUniform1i(int tid, str uniform, int x)            24011
```

- tid 0 is the activator. Each returns how many actors it changed or set. A material GLDEFS does not
  declare, a uniform the actor's material lacks, and too few arguments are refused.
- `""` or `"none"` takes an actor's material away. A new material lets the actor's own values go.

## State

- `AActor::fua_MaterialName` is DECORATE's (`fua_Material`, `thingdef_properties.cpp`), resolved at
  spawn into `fua_material` (`InitActor`), since GLDEFS is read after DECORATE. Both indices are kept one
  up, so a zeroed actor has none.
- An actor whose uniforms a script set holds a slot of `level.fua_actorMaterials` (`InstanceTable`,
  `ComputeAcquireInstance`), given back when it is destroyed (`ReleaseActor`) or takes another material.
  An actor with none reads its material's defaults.
- Savegames keep each actor's material by name and the uniforms it set (`SerializeActor`, SAVEVER 4527).
- Online, the server sends only changes (`SVC2_FUASETACTORMATERIAL`, `SVC2_FUASETACTORUNIFORM`), and a
  joining player each actor's material when it is not its class's and its own values
  (`SendActorMaterialTo`). A dedicated server reads the `fua_Material` blocks alone
  (`ReadDeclarations`, `ComputePostProcessDeclarations`), so it refuses the names a client would;
  `fua_actormaterials_reread` does that on any build.

## Drawing

- The render copy takes `fua_material` and `fua_materialSlot` per actor and the slot table by generation
  (`features/gamecopy`, `LevelActorMaterials`).
- `spritebuild.cpp`'s `ActorMaterialOf` puts the material's shader (`hwshaders::ActorMaterialShader`) and
  the actor's row (`ComputeActorUniformRow`) on its pieces. `FillPieceDataWith` draws with that shader and
  writes the row into `ScenePieceData::actorRow`, which the vertex stage hands on as `vActorRow`.
- `UploadActorUniforms` fills the rows (`ComputeActorUniformRows`) whenever the copy or the declarations
  changed, into the sky-hole buffer after its holes (`FUA_ACTOR_ROWS_STR`). A buffer of their own, read only
  by a material's program, came out as zeros there: a pipeline drawing with the plain one's bindings must
  read nothing the plain one does not.
- A program reads each uniform as `fuaActorUniform(i)` (`ComputeUniformLines`, `ShaderUniform::perActor`).

Scenario `hwactormaterials`.

## Files

- `computation/actormaterial_compute`: the ACS numbers and arguments, the slot table, and the GPU rows.
- `actormaterials.{h,cpp}`: the declarations, an actor's material and values, the ACS calls, what a
  client is sent and applies, and savegames.

## In-engine hooks

- `actor.h`: `fua_MaterialName`, `fua_material`, `fua_materialSlot`. `p_mobj.cpp`: `InitActor` in
  `StaticSpawn`, `ReleaseActor` in `Destroy`, `SerializeActor` in `Serialize`.
- `thingdef/thingdef_properties.cpp`: the `fua_Material` property.
- `g_level.h`/`g_level.cpp`: `FLevelLocals::fua_actorMaterials`, cleared by `G_InitLevelLocals`.
- `g_game.cpp` `GAME_ResetMap`: `ResetRound`, every actor back on its DECORATE material.
- `p_acs.cpp`: `ACSF_fua_SetActorMaterial` to `ACSF_fua_SetActorUniform1i`.
- `protocolspec/spec.misc.txt`, `network_enums.h`, `sv_commands.{h,cpp}`, `cl_main.cpp`: the two
  commands. `sv_main.cpp`: `SendActorMaterialTo` for each actor a joiner is sent.
- `version.h`: SAVEVER 4527.
- `features/gldefs/gldefs.cpp`: the `fua_material` tag calls `hwshaders::ParseActorMaterial`, and a
  restart calls `ClearDeclarations`. `r_nullrenderer.h`: `ReadDeclarations`.
- `features/postprocess`: `ForEachDefsPart`, the GLDEFS lumps a server reads, shared with its own.
- `features/hwshaders`: `ParseActorMaterial`, `ActorMaterialShader`, `UserShaderKey::actorMaterial`.
- `features/gamecopy`: the two actor fields and `LevelActorMaterials`.
- `features/levelmesh/staticmesh.h`: `MeshPiece::actorShader` and `actorRow`.
- `features/sprites/spritebuild.cpp`: `ActorMaterialOf` on sprites and models.
- `features/hwrender`: `ScenePieceData::actorRow` (`spritepack_compute`, refused by the packed form),
  `dgupload.cpp`'s `FillPieceDataWith`, `dgviews.cpp`'s `UploadActorUniforms`, `dgpipelines.cpp`'s sky-hole
  buffer sized for the rows, `dgshared.h`'s `PieceData::user` and `FUA_ACTOR_ROWS_STR`, and `dgscene_glsl.h`'s
  `fuaActorUniform` and `vActorRow`.
- `src/CMakeLists.txt`: `actormaterials.cpp` in the engine's sources.

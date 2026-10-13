# features/models

Models as game data, which both renderers read and each draws its own way. This was all in `gl/models/`,
so the backend's model path read GL's vertex buffer class to find a model's vertices.

- `modelgeometry.h`: `FModelVertex`, and `FModelGeometry`, a model's every keyframe of vertices back to back
  and, for an indexed format, the indices its surfaces are drawn by. It lives on the CPU.
- `models.h`, `md2model.cpp`, `md3model.cpp`, `voxelmodel.cpp`: the MD2/DMD, MD3 and voxel formats, moved
  from `gl/models/`. Each loads its file, fills its geometry once (`BuildGeometry`), and says which surfaces
  a pair of frames draws (`FrameSurfaces`), whether its frames blend (`Interpolates`, not a voxel's) and
  whether its skin is sampled unfiltered (`NoFilter`, a voxel's palette). None of it calls GL.
- `modeldef.cpp`: MODELDEF and the voxels VOXELDEF names (`gl_InitModels`), the sprite frame to model lookup
  (`gl_FindModelFrame`, `gl_IsHUDModelForPlayerAvailable`), the skins (`LoadSkin`), every model loaded
  (`ModelCount`, `ModelAt`), and the cvars both renderers read (`gl_use_models`,
  `gl_interpolate_model_frames`). The names are GL's, kept so GL's callers read them unchanged.

GL's `gl/models/gl_models.cpp` keeps a vertex buffer per model, made from its geometry on first draw, and
draws a frame's surfaces in one function for all three formats. The backend lerps the same geometry on the
CPU (`features/levelmesh/modelmesh.cpp`, through `features/sprites/modelbuild.cpp`). `fua_sprite_check`
compares every vertex of every surface the two make: 2,136 surfaces on scenario `modelstandalone`, 0 differed.

## In-engine hooks

- `r_data/sprites.cpp`: `gl_InitModels` at sprite init, now from here in every build (`posix/sdl/glstubs.cpp`
  no longer stubs it).
- `gl/models/gl_models.h`: includes `models.h`; `gl_RenderModel` and `gl_RenderHUDModel` stay GL's.
- `gl/data/gl_vertexbuffer.h` `FModelVertexBuffer`: built from a model's `FModelGeometry`.
- `gl/scene/gl_observer.h` `ModelSurface`: hands the check the model's geometry rather than GL's buffer.
- `gl/scene/gl_sprite.cpp`, `gl/scene/gl_scene.cpp`: `gl_FindModelFrame` and `gl_IsHUDModelForPlayerAvailable`.

# tools/web

Tools for the browser build. `docs/web-port-design.md` has the plan they serve.

## Emscripten

The web build pins Emscripten **4.0.0**, the version Diligent's own web CI builds with. Install emsdk outside the repo and point `EMSDK` at it:

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install 4.0.0 && ./emsdk activate 4.0.0
export EMSDK=$PWD
```

The scripts need CMake and Ninja on `PATH`. On Windows, Visual Studio's copies work from Git Bash.

## serve.mjs

```bash
node tools/web/serve.mjs <dir> [port]
```

Serves a build from `localhost`. It sends wasm's MIME type, plus the cross-origin isolation headers that Emscripten threads need (COOP `same-origin`, COEP `require-corp`).

## smoke.mjs

```bash
node tools/web/smoke.mjs <url> [--until REGEX] [--seconds N] [--shot out.png] [--headless false]
```

Loads a page in headless Chrome with WebGPU on and prints its console.
- It stops at the first console line matching `--until`, or when time runs out, and saves a screenshot if asked.
- The exit status is 1 when `--until` never matched.

## webgpu-check

```bash
tools/web/webgpu-check/build.sh
```

Builds Diligent's WebGPU backend for the browser on its own, into `build-webgpu-check/`.
- `triangle.html` attaches to a device created by the page and draws the engine's GLSL triangle. It proves the toolchain, the device hand-off and the in-module shader conversion:

  ```bash
  node tools/web/serve.mjs build-webgpu-check 8088
  node tools/web/smoke.mjs http://localhost:8088/triangle.html --until "presented 3 frames"
  ```

- `shadercheck.js` runs shaders through Diligent's WebGPU conversion (glslang to SPIR-V, Tint to WGSL). It prints each failure, and for a vertex stage the highest output location it uses. Feed it the engine's own shaders, dumped by a launch with `+set fua_dg_shaderdump <dir>`:

  ```bash
  node build-webgpu-check/shadercheck.js <dir>/*.vert <dir>/*.frag
  ```

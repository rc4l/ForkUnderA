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

## Building and running

```bash
EMSDK=/path/to/emsdk ./web_compile.sh
EMSDK=/path/to/emsdk ./web_run.sh [--no-build] [--no-open] [--port N] [query]
```

`web_compile.sh` builds the `web` preset into `build-web/` and stages the page, module and data into `dist-web/`. `web_run.sh` builds when anything changed, serves `dist-web/` and opens the browser at it, with the query on the address (such as `map=MAP01`).

## serve.mjs

```bash
node tools/web/serve.mjs <dir> [port]
```

Serves a build from `localhost`. It sends wasm's MIME type, plus the cross-origin isolation headers that Emscripten threads need (COOP `same-origin`, COEP `require-corp`).

## smoke.mjs

```bash
node tools/web/smoke.mjs <url> [--until REGEX] [--seconds N] [--shot out.png] [--headless false] [--evaluate JS --after N] [--profile DIR] [--software true] [--downloadDir DIR]
```

Loads a page in headless Chrome with WebGPU on and prints its console.
- It stops at the first console line matching `--until`, or when time runs out, and saves a screenshot if asked.
- The exit status is 1 when `--until` never matched.
- `--evaluate` runs JavaScript in the page `--after` seconds in (10 by default), such as `Module.fuaCommand("map MAP02")`, and prints its result.
- `--profile` keeps the browser profile in DIR between runs, so a second run sees what the first stored.
- `--software true` puts WebGPU on SwiftShader, Chrome's CPU renderer.
- `--downloadDir DIR` keeps the page's downloads in DIR, and the run waits for them to finish.
- The page can ask for input a person would give by logging `check: click` (the page's middle) or `check: press Tab` (also `Escape`, `Enter`). Input from inside the page doesn't count as a person's, so a click can't start the sound that way.

## check.mjs

```bash
node tools/web/check.mjs [dist-web] [--software]
```

Serves a staged build and runs the browser checks in headless Chrome, in order on one profile: the game boots and its screenshot shows a level, the sound waits for a click and plays after one, Tab stays in the game, the screenshot command downloads the frame, a save gets its picture, quitting stops the loop and offers to play again, a setting survives quitting, leaving the page and reloading, a recoverable error leaves the game running, and a fatal one stops it and shows on the page. It prints PASS or FAIL for each, with the console's last lines (and the screenshot, for the drawing check) for a failure, and exits 1 if any failed. `--software` runs WebGPU on SwiftShader, for a machine with no GPU; CI's `build-web` job runs it that way.

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

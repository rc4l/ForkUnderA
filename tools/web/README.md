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

`web_compile.sh` builds the `web` preset into `build-web/` and stages the page, module and data into `dist-web/`. `ZX_WITH_SYMBOLS=1` keeps function names in the wasm for readable crash stacks, `JOBS=N` caps the compile, and `ZX_KEEP_GOING=1` reports every compile error at once. `web_run.sh` builds when anything changed, serves `dist-web/` and opens the browser at it, with the query on the address (such as `map=MAP01`).

## serve.mjs

```bash
node tools/web/serve.mjs <dir> [port]
```

Serves a build from `localhost`. It sends wasm's MIME type, plus the cross-origin isolation headers that Emscripten threads need (`ISOLATION_HEADERS` in `headers.mjs`, which the demo site's Worker sends too).

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
- Chrome always runs muted, since a headless Chrome still plays to the machine's speakers; a run's caller can let the page's audio start without a click (`smoke({ autoplay })`).
- `--downloadDir DIR` keeps the page's downloads in DIR, and the run waits for them to finish.
- The page can ask for input a person would give by logging `check: click` (the page's middle), `check: press Tab` (also `Escape`, `Enter`) or `check: resize 900x700` (the window). Input from inside the page doesn't count as a person's, so a click can't start the sound that way.

## stage.mjs

```bash
node tools/web/stage.mjs [build-web] [dist-web] [--wads a.wad,b.wad]
```

Copies the module, the page and the data the page writes into the engine's file system, with its manifest. `--wads` adds files of your own, such as an IWAD and a map the query then names (`?iwad=doom2.wad&file=map.wad&map=MAP01`).

## demo/

```bash
node tools/web/demo/stage.mjs [dist-web]
npx wrangler deploy -c tools/web/demo/wrangler.jsonc
node tools/web/demo/verify.mjs [url] [--no-browser]
```

The demo site, a Cloudflare Worker serving a staged page under `/demo/` at the custom domain `wrangler.jsonc` names.
- The Worker adds the cross-origin isolation headers to every response, which static hosts such as GitHub Pages cannot send.
- Workers' static assets take no file over `ASSET_LIMIT` (`stage.mjs`), so the stage stores each larger one, such as Freedoom's IWAD, as `<name>.gz`, which the Worker sends gzipped for the browser to unpack.
- `stage.mjs` writes into the folder `wrangler.jsonc` uploads. CI's `ForkUnderA-web` artifact is a staged page to start from (`gh run download <run> -n ForkUnderA-web -D <dir>`).
- `verify.mjs` checks the deployed site against that folder as a player meets it, and exits 1 on any problem:
  - the bare address redirects to the page, keeping its query;
  - every file comes back isolated and unchanged, with a 304 for a revisit;
  - Chrome starts the page twice on one profile.
- `npx wrangler dev` from the folder serves the same thing locally. The Worker's logic is in `respond.mjs`, since its entry may export only its handler.

## probe.mjs

```bash
node tools/web/probe.mjs [dist-web] [--query Q] [--run "god; wait 1000; +attack"] [--autoplay] [--software] [--seconds N] [--browser chrome|firefox] [--wad a.wad,b.wad] [--wgsl FIND=>REPLACE] [--shots DIR]
node tools/web/probe.mjs [dist-web] [--query Q] --soak reload|TEXT [--cycles N] [--settle MS] [--seconds N]
```

Plays a staged page through console lines (with `wait N` between them, in milliseconds) and reports what it did: what the lines printed, each sound it started, any started again, the screen textures it held before and after, and its WebGPU errors. `--autoplay` lets its audio run without a click. Two steps look rather than act: `frames` prints the browser's frame pacing since the last `frames` (median, p99 and longest gap, and the gaps over 20 and 50 ms), and `level` the RMS the audio output carried since the last `level`. A step `ab CVAR A B` frames the view with the cvar at A and at B, sets it back, and prints how far the two frames sit apart (16-pixel block means, as the scenarios judge); `--shots DIR` keeps the frames. The page has the focus, as a player's does. `--browser firefox` plays it in Firefox instead (`firefox.mjs`, over WebDriver BiDi): headless Firefox offers no WebGPU adapter, so its window opens and is moved off the screen, silent.

`--wad` serves WADs from anywhere to the page as if they were staged, so a template map plays on any build (`--query "file=a.wad&map=MAP01"`). `--wgsl FIND=>REPLACE` rewrites every WGSL shader the page makes, to try a shader change without building again: `--wgsl "(1.0f / gl_FragCoord.w)=>vPixelPos.w"` tried queue #68's fix on a build from before it.

`--soak` instead repeats an action on the query's map (`reload` loads it again; other text is console lines separated by `|`, and a `check: resize WxH` line resizes the page), marks memory after each, and prints the engine's `fua_soak` verdict, exiting 1 when something kept growing. Chrome runs with `--expose-gc`, so each mark comes after the page's garbage is collected.

The page code, the watches and the soak among it, is shared with the checks (`page.mjs`).

## check.mjs

```bash
node tools/web/check.mjs [dist-web] [--software] [--only TEXT] [--browser chrome|firefox]
```

Serves a staged build and runs `CHECKS` (in `check.mjs`, each named for what it proves) in headless Chrome, in order on one profile, since a later check can read what an earlier one stored.
- It prints PASS or FAIL for each, with the console's last lines (and the screenshot, where the check judges one) for a failure, and exits 1 if any failed.
- An error the page's WebGPU device reports fails whichever check it came in.
- `--only TEXT` runs the checks whose names contain TEXT.
- `--software` runs WebGPU on SwiftShader, for a machine with no GPU; CI's `build-web` job runs it that way.
- `--browser firefox` runs the checks marked `firefox` in Firefox and skips the rest. Firefox's WebGPU is not Chrome's, and CI has no Firefox, so run it here after touching the shaders.
- A check can carry its own map: `query` opens it with its own query, and `serveData(name, bytes)` (`page.mjs`) as its `init` serves the WAD as if staged; the light grid check plays `scenarios/pointlight.mjs`'s hall that way.

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

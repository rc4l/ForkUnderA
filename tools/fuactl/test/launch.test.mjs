import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { resolveEngine, engineArgs } from "../src/launch.mjs";

// Run `fn` with cwd moved to `dir` and FUACTL_ENGINE cleared, then put both back.
function inDir(dir, fn) {
  const cwd = process.cwd();
  const env = process.env.FUACTL_ENGINE;
  delete process.env.FUACTL_ENGINE;
  process.chdir(dir);
  try {
    return fn();
  } finally {
    process.chdir(cwd);
    if (env !== undefined) process.env.FUACTL_ENGINE = env;
  }
}

// A repo root with one staged binary at `rel`, plus the tools/fuactl folder a command may run from.
function repoWith(rel) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-resolve-")));
  const bin = path.join(root, rel);
  fs.mkdirSync(path.dirname(bin), { recursive: true });
  fs.writeFileSync(bin, "");
  fs.mkdirSync(path.join(root, "tools", "fuactl"), { recursive: true });
  return { root, bin };
}

// [rc4l] The list held only the mac .app, so on Windows every command needing the binary died with
// "engine binary not found" -- including `ui screenshot`, which only wants the folder the PNG lands
// in. Swept over all three so a platform cannot be added to the compile scripts and missed here.
for (const rel of [
  "build/ForkUnderA.app/Contents/MacOS/forkundera",
  "dist-windows/forkundera.exe",
  "dist-linux/forkundera",
]) {
  test(`resolveEngine finds a binary staged at ${rel}`, () => {
    const { root, bin } = repoWith(rel);

    assert.equal(inDir(root, resolveEngine), bin, "from the repo root");
    assert.equal(inDir(path.join(root, "tools", "fuactl"), resolveEngine), bin, "from tools/fuactl");
  });
}

test("resolveEngine prefers FUACTL_ENGINE over anything staged", () => {
  const { root, bin } = repoWith("dist-windows/forkundera.exe");
  const override = path.join(root, "elsewhere", "forkundera");
  fs.mkdirSync(path.dirname(override), { recursive: true });
  fs.writeFileSync(override, "");

  const cwd = process.cwd();
  const env = process.env.FUACTL_ENGINE;
  process.env.FUACTL_ENGINE = override;
  process.chdir(root);
  try {
    assert.equal(resolveEngine(), override);
    assert.notEqual(resolveEngine(), bin);
  } finally {
    process.chdir(cwd);
    if (env === undefined) delete process.env.FUACTL_ENGINE; else process.env.FUACTL_ENGINE = env;
  }
});

// ---------------------------------------------------------------- the command line

// Read the value `+set <name> V` sets, or undefined when the cvar is never set.
function setValue(args, name) {
  for (let i = 0; i + 2 < args.length; i++) {
    if (args[i] === "+set" && args[i + 1] === name) return args[i + 2];
  }
  return undefined;
}

test("a harness instance keeps the OS mouse out of the game", () => {
  // use_mouse is the one that stops a stray cursor: on Windows it gates OS mouse buttons, movement
  // and wheel out of the GUI (i_input.cpp), and in a live level it stops the view being turned.
  const args = engineArgs({}, "x.ini");

  assert.equal(setValue(args, "use_mouse"), "0");
  assert.equal(setValue(args, "use_joystick"), "0");
});

test("a harness instance does NOT disable the menu mouse it injects into", () => {
  // [rc4l] m_use_mouse used to be forced to 0 here. menu.cpp drops every GUI mouse event when it is
  // 0, injected ones included, so `ui click` reported {"clicked":[x,y]} and did nothing -- on every
  // platform. Setting it at all is the bug; the physical cursor is use_mouse's job, above.
  assert.equal(setValue(engineArgs({}, "x.ini"), "m_use_mouse"), undefined);
});

test("every instance gets its own config, never the user's ini", () => {
  // Without -config the engine reads the user's real ini and writes its overrides back on exit,
  // which both poisons a clean baseline and breaks the mouse of whoever owns the machine.
  const args = engineArgs({}, "/tmp/run-7/fua-bridge.ini");
  const at = args.indexOf("-config");

  assert.notEqual(at, -1, "-config is not optional");
  assert.equal(args[at + 1], "/tmp/run-7/fua-bridge.ini");
});

test("seed and map reach the command line, and a client connects instead of loading a map", () => {
  const local = engineArgs({ seed: 777, map: "MAP07", skill: 4 }, "x.ini");
  assert.deepEqual(local.slice(local.indexOf("-rngseed"), local.indexOf("-rngseed") + 2), ["-rngseed", "777"]);
  assert.equal(local[local.indexOf("+map") + 1], "MAP07");
  assert.equal(local[local.indexOf("-skill") + 1], "4");

  const client = engineArgs({ connect: "127.0.0.1:10666" }, "x.ini");
  assert.equal(client[client.indexOf("+connect") + 1], "127.0.0.1:10666");
  assert.equal(client.includes("+map"), false, "a joining client must not also load a local map");
});

test("cvars are set BEFORE the map loads, not after it", () => {
  // [rc4l] The engine runs `+` arguments in the order given, and a level-start cvar set after +map
  // does nothing to the level already standing. sv_nomonsters is the one that bites: an instance
  // asked for no monsters came up full of them, and they shove the player off the position a repro
  // was recorded at and shoot whatever is being measured.
  //
  // A comment above the cvar loop claimed this ordering for a long time while the code did the
  // opposite, so it is asserted here rather than described there.
  const args = engineArgs({ map: "MAP07", cvars: { sv_nomonsters: 1, fua_vulkan: 1 } }, "x.ini");

  for (const key of ["sv_nomonsters", "fua_vulkan"]) {
    const at = args.indexOf(key);
    assert.notEqual(at, -1, `${key} never reached the command line`);
    assert.ok(at < args.indexOf("+map"), `${key} is set after +map, so the level never sees it`);
  }
});

test("resolveEngine names every path it tried when it finds nothing", () => {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-resolve-")));

  assert.throws(() => inDir(root, resolveEngine), (err) => {
    // The message is the whole fix for a mis-set path, so it has to list the real candidates.
    assert.match(err.message, /FUACTL_ENGINE/);
    assert.match(err.message, /dist-windows/);
    assert.match(err.message, /dist-linux/);
    assert.match(err.message, /ForkUnderA\.app/);
    return true;
  });
});

import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { resolveEngine } from "../src/launch.mjs";

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

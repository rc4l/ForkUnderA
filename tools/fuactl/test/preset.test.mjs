import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { resolvePreset, defaultPwadStore } from "../src/play.mjs";

// [rc4l] A catalogue with one entry and a store holding the files named, in fresh temp dirs.
function setup(entry, stored) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "preset-"));
  const cat = path.join(root, "catalogue"), store = path.join(root, "store");
  fs.mkdirSync(path.join(cat, "e"), { recursive: true });
  fs.mkdirSync(store);
  fs.writeFileSync(path.join(cat, "e", "addon.json"), JSON.stringify(entry));
  for (const f of stored) fs.writeFileSync(path.join(store, f), "x");
  return { cat, store };
}

test("an entry with top-level files is its own only variant", () => {
  // [rc4l] catalogue/whodunit is shaped this way, and --preset used to throw on it.
  const { cat, store } = setup({ files: [{ name: "a.pk3" }, { name: "b.wad" }] }, ["a.pk3", "b.wad"]);
  assert.deepEqual(resolvePreset(cat, store, "e").map((p) => path.basename(p)), ["a.pk3", "b.wad"]);
});

test("an entry with variants takes the default one, or the one asked for", () => {
  const entry = { variants: [{ id: "x", files: [{ name: "x.pk3" }] }, { id: "y", default: true, files: [{ name: "y.pk3" }] }] };
  const { cat, store } = setup(entry, ["x.pk3", "y.pk3"]);
  assert.deepEqual(resolvePreset(cat, store, "e").map((p) => path.basename(p)), ["y.pk3"]);
  assert.deepEqual(resolvePreset(cat, store, "e", "x").map((p) => path.basename(p)), ["x.pk3"]);
  assert.throws(() => resolvePreset(cat, store, "e", "nope"), /no such variant: nope/);
});

test("a file missing from the store is named, never quietly dropped", () => {
  const { cat, store } = setup({ files: [{ name: "a.pk3" }, { name: "gone.pk3" }] }, ["a.pk3"]);
  assert.throws(() => resolvePreset(cat, store, "e"), /missing, fetch with fua_download: .*gone\.pk3/);
});

test("the default store is where the engine's downloader puts wads, on every platform", () => {
  assert.equal(defaultPwadStore("win32", { LOCALAPPDATA: "/la" }, "/h"), path.join("/la", "ForkUnderA/pwads"));
  assert.equal(defaultPwadStore("win32", {}, "/h"), path.join("/h", "AppData/Local", "ForkUnderA/pwads"));
  assert.equal(defaultPwadStore("darwin", {}, "/h"), path.join("/h", "Library/Application Support/ForkUnderA/pwads"));
  assert.equal(defaultPwadStore("linux", {}, "/h"), path.join("/h", ".config/ForkUnderA/pwads"));
});

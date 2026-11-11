// [Doruk-Aksoy] The files scenarios/dbedge.mjs starts each case from: its pk3, and the database and outboxes a crashed, stopped or stock server leaves.
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { buildDbEdgePk3, prepareCase, writeOutbox } from "../scenarios/dbedge.mjs";

const scratch = () => fs.mkdtempSync(path.join(os.tmpdir(), "fua-dbedge-test-"));
const one = (file, sql) => {
  const db = new DatabaseSync(file);
  try { return db.prepare(sql).all(); } finally { db.close(); }
};

test("the pk3 carries the compiled library and loads it", () => {
  const data = buildDbEdgePk3().toString("latin1");
  assert.ok(data.includes("acs/dbedge.o"));
  assert.ok(data.includes("LOADACS.txt"));
  assert.ok(data.includes("dbedge\n"));
});

test("fresh and hardkill start from no file", () => {
  const dir = scratch();
  for (const name of ["fresh", "hardkill"]) {
    prepareCase(name, path.join(dir, `${name}.db`));
    assert.equal(fs.existsSync(path.join(dir, `${name}.db`)), false);
  }
});

test("stockrows holds the four kinds of stock timestamp", () => {
  const file = path.join(scratch(), "stock.db");
  prepareCase("stockrows", file);
  assert.deepEqual(one(file, "SELECT KeyName, Timestamp FROM Zandronum ORDER BY KeyName").map((r) => [r.KeyName, r.Timestamp]),
    [["future", "4102444800"], ["garbagestamp", "abc"], ["juliantext", "1700000000.12345"], ["nullstamp", null]]);
});

test("resume leaves an outbox whose first batch the file already holds", () => {
  const file = path.join(scratch(), "resume.db");
  prepareCase("resume", file);
  assert.deepEqual(one(file, "SELECT Server, Seq FROM fua_Applied").map((r) => [r.Server, r.Seq]), [["deadbeefdeadbeefdeadbeefdeadbeef", 1]]);
  assert.deepEqual(one(`${file}.fua-outbox-0`, "SELECT Seq, Kind, KeyName FROM Ops ORDER BY Seq").map((r) => [r.Seq, r.Kind, r.KeyName]), [[1, 0, "resumed"], [2, 2, "rc"]]);
  assert.deepEqual(one(`${file}.fua-outbox-0`, "SELECT Key, Value FROM Meta ORDER BY Key").map((r) => [r.Key, r.Value]), [["next", "3"], ["server", "deadbeefdeadbeefdeadbeefdeadbeef"]]);
});

test("orphan leaves a corrupt slot 1 and a stopped server's slot 3", () => {
  const file = path.join(scratch(), "orphan.db");
  prepareCase("orphan", file);
  assert.equal(fs.readFileSync(`${file}.fua-outbox-1`).length, 4096);
  assert.deepEqual(one(`${file}.fua-outbox-3`, "SELECT Value FROM Ops").map((r) => r.Value), ["replayed"]);
  assert.equal(fs.existsSync(`${file}.fua-outbox-0`), false);
});

test("an outbox keeps each op's place in its batch", () => {
  const file = path.join(scratch(), "outbox.db");
  writeOutbox(file, "id", 9, [[4, 1.5, 2, "ns", "a", "", 3], [4, 1.5, 0, "ns", "b", "v", 0]]);
  assert.deepEqual(one(file, "SELECT Seq, Idx, KeyName FROM Ops ORDER BY Idx").map((r) => [r.Seq, r.Idx, r.KeyName]), [[4, 0, "a"], [4, 1, "b"]]);
});

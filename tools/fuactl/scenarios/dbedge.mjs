// [Doruk-Aksoy] The ACS database through the engine's write queue at its edges: each case starts the engine on a database file, runs fixtures/dbedge's scripts, and checks the console and the file.
// [Doruk-Aksoy] Cases: fresh (reads while the writer lags, null names, 64-bit overflow, transaction misuse, a transaction its level left open, UNLOADING writes, 5000 writes in a tic, a clean quit),
// [Doruk-Aksoy] hardkill (a killed process's writes land on restart), stockrows (rows with NULL, old, garbage and future stamps), resume (a crashed server's outbox, part applied), orphan (a stopped server's outbox and a corrupt one).
//
// usage:  node scenarios/dbedge.mjs [port]
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { fileURLToPath } from "node:url";
import { pk3 } from "../src/udmf.mjs";
import { launchInstance, stopInstance } from "../src/launch.mjs";
import { attach } from "../src/session.mjs";
import * as cap from "../src/capture.mjs";
import { runScenarioIfMain, FREEDOOM2, MUTE } from "../src/template.mjs";

const FIXTURE = fileURLToPath(new URL("../fixtures/dbedge/dbedge.o", import.meta.url));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function buildDbEdgePk3() {
  return pk3([{ name: "acs/dbedge.o", data: fs.readFileSync(FIXTURE) }, { name: "LOADACS.txt", data: "dbedge\n" }]);
}

const TABLE = "CREATE TABLE Zandronum(Namespace text, KeyName text, Value text, Timestamp text, PRIMARY KEY (Namespace, KeyName))";
// [Doruk-Aksoy] DatabaseOpKind's numbers, as an outbox stores them.
const SET = 0, INCREMENT = 2;

// [Doruk-Aksoy] An outbox as a server that died left it: its id, its next batch number and its batches, each [seq, stamp, kind, ns, key, value, delta].
export function writeOutbox(file, server, next, ops) {
  const db = new DatabaseSync(file);
  db.exec("CREATE TABLE Meta(Key text PRIMARY KEY, Value text)");
  db.exec("CREATE TABLE Ops(Seq integer, Idx integer, Stamp real, Kind integer, Namespace text, KeyName text, Value text, Delta integer, PRIMARY KEY (Seq, Idx))");
  db.prepare("INSERT INTO Meta VALUES('server', ?)").run(server);
  db.prepare("INSERT INTO Meta VALUES('next', ?)").run(String(next));
  const insert = db.prepare("INSERT INTO Ops VALUES(?,?,?,?,?,?,?,?)");
  ops.forEach((op, i) => insert.run(op[0], i, op[1], op[2], op[3], op[4], op[5], op[6]));
  db.close();
}

// [Doruk-Aksoy] The database file (and outboxes beside it) each case starts from; fresh starts from none.
export function prepareCase(name, file) {
  if (name === "fresh" || name === "hardkill") return;
  const db = new DatabaseSync(file);
  db.exec(TABLE);
  const row = db.prepare("INSERT INTO Zandronum VALUES(?,?,?,?)");
  if (name === "stockrows") {
    row.run("edge", "nullstamp", "old", null);
    row.run("edge", "juliantext", "old", "1700000000.12345");
    row.run("edge", "garbagestamp", "old", "abc");
    row.run("edge", "future", "old", "4102444800");
  } else if (name === "resume") {
    db.exec("CREATE TABLE fua_Applied(Server text PRIMARY KEY, Seq integer)");
    db.exec("INSERT INTO fua_Applied VALUES('deadbeefdeadbeefdeadbeefdeadbeef', 1)");
    row.run("edge", "resumed", "keep", "1800000000");
    writeOutbox(`${file}.fua-outbox-0`, "deadbeefdeadbeefdeadbeefdeadbeef", 3,
      [[1, 1700000000, SET, "edge", "resumed", "should not apply", 0], [2, 1700000001, INCREMENT, "edge", "rc", "", 5]]);
  } else if (name === "orphan") {
    fs.writeFileSync(`${file}.fua-outbox-1`, Buffer.alloc(4096, "x"));
    writeOutbox(`${file}.fua-outbox-3`, "cafecafecafecafecafecafecafecafe", 2, [[1, 1700000000, SET, "edge", "orphaned", "replayed", 0]]);
  }
  db.close();
}

// [Doruk-Aksoy] Reads the file as another process would.
function query(file, sql, ...args) {
  const db = new DatabaseSync(file, { readOnly: true });
  try { return db.prepare(sql).get(...args); } finally { db.close(); }
}
const get = (file, ns, key) => (query(file, "SELECT Value FROM Zandronum WHERE Namespace=? AND KeyName=?", ns, key) || { Value: "<missing>" }).Value;
const count = (file, ns) => query(file, "SELECT COUNT(*) AS n FROM Zandronum WHERE Namespace=?", ns).n;
const outboxOps = (file) => query(`${file}.fua-outbox-0`, "SELECT COUNT(*) AS n FROM Ops").n;
const lineOf = (text, prefix) => (text.match(new RegExp(`^.*${prefix}.*$`, "m")) || [""])[0].trim();

// [Doruk-Aksoy] One engine on the case's file; "clean" ends it with the console's quit (shutdown handlers run), "kill" terminates it.
async function withEngine(opts, file, check, steps, quit = "clean") {
  const inst = await launchInstance({ port: opts.port, iwad: FREEDOOM2, map: "MAP01", extraArgs: ["-file", opts.pk3Path], cvars: { ...MUTE, ...(opts.cvars || {}), databasefile: file } });
  const c = await attach(inst);
  const log = () => { try { return fs.readFileSync(inst.log, "utf8"); } catch { return ""; } };
  try {
    await cap.waitInLevel(c, 120000);
    await steps({ log, run: (t) => cap.exec(c, t), puke: (n) => cap.exec(c, `puke ${n}`) });
    if (quit === "clean") {
      c.rpc("console.exec", { text: "quit" }).catch(() => {});
      for (let i = 0; i < 100 && inst.proc.exitCode == null; i++) await sleep(100);
      check("the engine quits on its own", inst.proc.exitCode != null, String(inst.proc.exitCode));
    }
  } finally {
    c.close();
    await stopInstance(inst);
  }
  return log();
}

export const CASES = {
  async fresh(opts, file, check) {
    const text = await withEngine(opts, file, check, async ({ log, run, puke }) => {
      for (const n of [951, 952, 953, 954, 956, 957]) await puke(n);
      await sleep(4500);
      await run("changemap MAP02");
      await sleep(2500);
      check("an UNLOADING script's write is in the file during the intermission", get(file, "edge", "unloads") === "1", get(file, "edge", "unloads"));
      await run("map MAP01");
      await sleep(2000);
      await puke(955);
      await sleep(1500);
      check("a transaction left open holds its write back while its level runs", get(file, "edge", "orphan") === "<missing>");
      await run("map MAP01");
      await sleep(2500);
      check("the level change ends the transaction its scripts left open", /still open when its level ended/.test(log()));
      check("...and its write reaches the file", get(file, "edge", "orphan") === "42", get(file, "edge", "orphan"));
    });
    // [Doruk-Aksoy] Looked at before any read below, since a reader's own connection would leave a -wal of its own.
    check("a clean quit closes the database (no -wal left)", !fs.existsSync(`${file}-wal`));
    const expect = [["EDGE sametic", "EDGE sametic 1", "a write reads back in its own tic"],
      ["EDGE pertic", "EDGE pertic cnt=70 mismatches=0", "70 tics of increments, sorts and ranks read right while the writer lags"],
      ["EDGE null", "EDGE null 7 8 empty=8 str=8", "an invalid string id is the empty string, not a crash"],
      ["EDGE bignow", "EDGE bignow 9.22337203685478e+18", "an increment past 64 bits reads as SQLite's REAL"],
      ["EDGE biglater", "EDGE biglater 9.22337203685478e+18", "...and reads the same once applied"],
      ["EDGE txn", "EDGE txn 1 2", "a transaction spanning tics keeps both writes"],
      ["EDGE delnow", "EDGE delnow [] 3", "a delete then an increment starts the key from zero"],
      ["EDGE dellater", "EDGE dellater 3", "...and reads the same once applied"],
      ["EDGE bigbatch", "EDGE bigbatch 5000 last=4999", "5000 writes in one tic read back through GetDBEntries"]];
    for (const [prefix, want, name] of expect) check(name, lineOf(text, prefix) === want, lineOf(text, prefix));
    check("End without Begin prints SQLite's message", /Error: cannot commit - no transaction is active/.test(text));
    check("Begin inside Begin prints SQLite's message", /Error: cannot start a transaction within a transaction/.test(text));
    for (const [key, want] of [["a", "1"], ["cnt", "70"], ["big", "9.22337203685478e+18"], ["t1", "1"], ["t2", "2"], ["d", "3"], ["unloads", "1"], ["orphan", "42"]])
      check(`the file holds edge/${key} = ${want}`, get(file, "edge", key) === want, get(file, "edge", key));
    check("the file holds the empty namespace's empty key = 8", get(file, "", "") === "8", get(file, "", ""));
    check("the file holds all 5000 keys of the big batch", count(file, "edgebig") === 5000, String(count(file, "edgebig")));
    check("a clean quit leaves the outbox empty", outboxOps(file) === 0, String(outboxOps(file)));
  },

  async hardkill(opts, file, check) {
    await withEngine(opts, file, check, async ({ puke }) => { await puke(957); await sleep(200); }, "kill");
    check("the killed process left its outbox unpruned", outboxOps(file) > 0, String(outboxOps(file)));
    const text = await withEngine(opts, file, check, async () => { await sleep(2500); });
    check("a restart brings in every write the killed process made durable, none twice", /queued through/.test(text) && count(file, "edgebig") === 5000 && get(file, "edgebig", "k4999") === "4999", String(count(file, "edgebig")));
  },

  async stockrows(opts, file, check) {
    const text = await withEngine(opts, file, check, async ({ puke }) => { await puke(958); await sleep(2500); });
    check("rows with NULL, old, garbage and future stamps all take the new write", lineOf(text, "EDGE stockrows") === "EDGE stockrows newnew", lineOf(text, "EDGE stockrows"));
    check("a stamp far ahead of this clock is warned about", /clock is more than a minute behind/.test(text));
    for (const key of ["nullstamp", "juliantext", "garbagestamp", "future"]) check(`the file holds edge/${key} = new`, get(file, "edge", key) === "new", get(file, "edge", key));
  },

  async resume(opts, file, check) {
    const text = await withEngine(opts, file, check, async ({ puke }) => { await sleep(1500); await puke(959); await sleep(500); });
    check("a crashed server's outbox is resumed, less the batch the file already holds", /Resuming 1 database batch/.test(text), lineOf(text, "Resuming"));
    check("the applied batch neither shows nor lands, the pending one does", lineOf(text, "EDGE resumed").startsWith("EDGE resumed [keep] rc=5"), lineOf(text, "EDGE resumed"));
    check("the file keeps the newer value and gains the increment", get(file, "edge", "resumed") === "keep" && get(file, "edge", "rc") === "5", `${get(file, "edge", "resumed")} ${get(file, "edge", "rc")}`);
  },

  async orphan(opts, file, check) {
    const text = await withEngine(opts, file, check, async ({ log, puke }) => {
      for (let i = 0; i < 20 && !/left in its outbox/.test(log()); i++) await sleep(500);
      await puke(959);
      await sleep(500);
    });
    check("the corrupt outbox at slot 1 is named and passed over", /fua-outbox-1 cannot be opened as a database/.test(text));
    check("the new server takes slot 0", /queued through .*fua-outbox-0/.test(text));
    check("the stopped server's outbox at slot 3 is replayed", /Applied 1 database batch\(es\) a stopped server left/.test(text));
    check("its write reads back and is in the file", lineOf(text, "EDGE resumed").includes("orphaned [replayed]") && get(file, "edge", "orphaned") === "replayed", lineOf(text, "EDGE resumed"));
  },
};

export async function compareDbEdge(opts) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fua-dbedge-"));
  const pk3Path = path.join(dir, "dbedge.pk3");
  fs.writeFileSync(pk3Path, buildDbEdgePk3());
  const lines = [];
  let failed = 0;
  for (const name of opts.cases || Object.keys(CASES)) {
    const file = path.join(dir, `${name}.db`).replace(/\\/g, "/");
    prepareCase(name, file);
    const check = (what, ok, detail = "") => {
      if (!ok) failed++;
      lines.push(`${ok ? "PASS" : "FAIL"}  ${name}: ${what}${detail ? `  (${detail})` : ""}`);
    };
    await CASES[name]({ ...opts, pk3Path }, file, check);
  }
  return { lines, failed };
}

export const SIDES = [{ name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "dbedge", port: 7876, sides: SIDES }), compareDbEdge);

// [Doruk-Aksoy] The flag-word scenario: its actors, script 1's checks, the log judge, and the offline flow's order.
import { test } from "node:test";
import assert from "node:assert/strict";
import { ACTORS, CHECKS, STRINGS, buildDecorate, buildMap, checkWords, judgeFlags, compareOffline, oldEngine, FIRST_TID, judgeMirrorFlags, judgeNetplay } from "../scenarios/decorateflags.mjs";
import { PCD } from "../src/acs.mjs";

test("every actor's flags and changes are written, and every check names a string", () => {
  const d = buildDecorate();
  for (const a of ACTORS) {
    assert.match(d, new RegExp(`actor ${a.cls}\\n`));
    for (const f of a.flags || []) assert.ok(d.includes(`\t+${f}\n`), `${a.cls} +${f}`);
    for (const [f, on] of Object.entries(a.change || {})) assert.ok(d.includes(`A_ChangeFlag("${f}", ${on ? 1 : 0})`), `${a.cls} ${f}`);
  }
  for (const c of CHECKS) assert.ok(STRINGS.indexOf(c.flag) >= 2);
  assert.equal(CHECKS.length, ACTORS.reduce((n, a) => n + Object.keys(a.want).length, 0));
});

test("the map places each actor at its tid and script 1 logs one line a check", () => {
  const m = buildMap();
  ACTORS.forEach((_, i) => assert.ok(m.things.some((t) => t.id === FIRST_TID + i && t.type === 31500 + i)));
  const words = checkWords();
  assert.equal(words.filter((w, i) => w === PCD.ENDLOG && words[i - 1] === PCD.PRINTNUMBER).length, CHECKS.length);
  assert.equal(words[words.length - 1], PCD.TERMINATE);
});

test("the judge wants every check logged at its value", () => {
  const all = CHECKS.map((c, i) => `zxflag ${i} ${c.want}`).join("\n");
  assert.deepEqual(judgeFlags(all), []);
  const first = CHECKS[0];
  const name = `${ACTORS[first.actor].cls} ${first.flag}`;
  assert.deepEqual(judgeFlags(all.replace(`zxflag 0 ${first.want}`, `zxflag 0 ${1 - first.want}`)), [`${name} is ${1 - first.want}, not ${first.want}`]);
  assert.deepEqual(judgeFlags(all.replace(`zxflag 0 ${first.want}`, "")), [`${name} not logged`]);
});

test("the older build comes from --old, else FUACTL_OLD_ENGINE", () => {
  assert.equal(oldEngine(["7884", "--old", "C:/a.exe"], {}), "C:/a.exe");
  assert.equal(oldEngine([], { FUACTL_OLD_ENGINE: "C:/b.exe" }), "C:/b.exe");
  assert.equal(oldEngine([], {}), null);
});

// [Doruk-Aksoy] compareOffline on fake engines: each answers script 1 with `logOf(engine)` and keeps the saves asked of it, unless `noSave` names it.
async function runOffline({ old = null, logOf, noSave = [] }) {
  const saved = new Set(), seen = [];
  const io = {
    exec: async (c, text) => {
      seen.push(`${c.engine}: ${text}`);
      const m = /^save "(.*)"$/.exec(text);
      if (m && !noSave.includes(c.engine)) saved.add(m[1]);
      return /^puke /.test(text) ? logOf(c.engine, seen) : "";
    },
    waitTics: async () => {}, sleep: async () => {}, waitInLevel: async () => {}, exists: (f) => saved.has(f),
  };
  const r = await compareOffline({ name: "t", wad: Buffer.alloc(0), port: 1, old }, { io, engine: "new.exe", wadPath: "x.pk3", dir: "D", launch: (engine, body) => body({ engine }) });
  return { ...r, seen };
}

const PASS_LOG = CHECKS.map((c, i) => `zxflag ${i} ${c.want}`).join("\n");

test("offline: the map as started and this build's own save pass, and no older build is a skip", async () => {
  const r = await runOffline({ logOf: () => PASS_LOG });
  assert.equal(r.failed, 0);
  assert.match(r.lines[0], /^PASS offline: /);
  assert.match(r.lines[1], /^SKIP older save/);
  assert.ok(r.seen.some((l) => /^new\.exe: load ".*own\.zds"$/.test(l)));
});

test("offline: an older build's save is written by it and loaded by this build", async () => {
  const r = await runOffline({ old: "C:/old/forkundera.exe", logOf: () => PASS_LOG });
  assert.equal(r.failed, 0);
  assert.match(r.lines[1], /^PASS older save: .* from old's save$/);
  assert.ok(r.seen.some((l) => /^C:\/old\/forkundera\.exe: save ".*old\.zds"$/.test(l)));
  assert.ok(r.seen.some((l) => /^new\.exe: load ".*old\.zds"$/.test(l)));
});

test("offline: wrong flags after a load, and a save never written, are faults", async () => {
  const wrong = PASS_LOG.replace("zxflag 0 1", "zxflag 0 0");
  const afterLoad = await runOffline({ old: "old.exe", logOf: (engine, seen) => (seen.some((l) => / load /.test(l)) ? wrong : PASS_LOG) });
  assert.equal(afterLoad.failed, 2);
  assert.match(afterLoad.lines[0], /own save: ZxFlags7 NODECAL is 0, not 1/);
  assert.match(afterLoad.lines[1], /^FAIL older save: ZxFlags7 NODECAL is 0, not 1/);
  const none = await runOffline({ old: "old.exe", logOf: () => PASS_LOG, noSave: ["new.exe", "old.exe"] });
  assert.match(none.lines[0], /own save: this build wrote no save/);
  assert.match(none.lines[1], /^FAIL older save: the older build wrote no save/);
});

test("judgeMirrorFlags names a check actor whose mirror draws other render words, or has none", () => {
  const server = [{ netid: 5, c: ACTORS[0].cls, rf: 1, rf2: 0 }, { netid: 6, c: ACTORS[1].cls, rf: 0, rf2: 1024 }, { netid: 7, c: "DoomPlayer", rf: 0, rf2: 0 }];
  assert.deepEqual(judgeMirrorFlags(server, [{ netid: 5, rf: 1, rf2: 0, mirror: 1 }, { netid: 6, rf: 0, rf2: 1024, mirror: 1 }]), []);
  assert.deepEqual(judgeMirrorFlags(server, [{ netid: 5, rf: 0, rf2: 0, mirror: 1 }, { netid: 6, rf: 0, rf2: 1024 }]),
    [`${ACTORS[0].cls}'s mirror draws rf 0 rf2 0, the server's actor 1 0`, `${ACTORS[1].cls} has no mirror`]);
});

test("judgeNetplay adds the mirrors only under the snapshot netcode, on every client", async () => {
  const flags = async (set, label) => ({ line: `PASS ${label}: flags`, failed: 0 });
  const server = [{ netid: 5, c: ACTORS[0].cls, rf: 1, rf2: 0 }];
  const good = [{ netid: 5, rf: 1, rf2: 0, mirror: 1 }], bad = [{ netid: 5, rf: 0, rf2: 0, mirror: 1 }];
  const set = (rowsByClient) => ({ server: { c: "s" }, clients: rowsByClient.map((r, i) => ({ c: r, role: i ? "observer" : "player", player: i })) });
  const rows = async (c) => (c === "s" ? server : c);
  assert.deepEqual(await judgeNetplay(set([bad]), "lan", { config: {} }, { flags, rows }), { line: "PASS lan: flags", failed: 0 });
  assert.equal((await judgeNetplay(set([good, good]), "snap", { config: { serverCvars: { fua_snap: 1 } } }, { flags, rows })).failed, 0);
  const r = await judgeNetplay(set([good, bad]), "snap", { config: { serverCvars: { fua_snap: 1 } } }, { flags, rows });
  assert.equal(r.failed, 1);
  assert.match(r.line, /^FAIL snap: mirrors: observer player 1: /);
  const both = await judgeNetplay(set([bad]), "snap", { config: { serverCvars: { fua_snap: 1 } } }, { flags: async () => ({ line: "FAIL snap: flags", failed: 1 }), rows });
  assert.match(both.line, /^FAIL snap: flags; mirrors: player player 0: /);
});

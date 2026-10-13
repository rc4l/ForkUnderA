import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { EventEmitter } from "node:events";
import { presentMonPath, ensurePresentMon, adminScript, runAdminScript, qpcFrequency, parsePresentMon, analyzePresentMon, alignSeconds, formatPresentMon,
  PRESENTMON_URL, COMPOSITOR } from "../src/present.mjs";

const HEAD = "\uFEFFApplication,ProcessID,PresentMode,TimeInQPC,MsBetweenDisplayChange,MsUntilDisplayed";
const INTEL = "Valid|CN=Intel Corporation, O=Intel Corporation, S=California, C=US";

// [rc4l] A PresentMon row of `app` at `s` seconds on a 10 MHz counter, shown when `held` (ms since the last screen change) is given.
function pm(app, s, held = null, mode = "Composed: Flip") {
  return `${app},1,${mode},${Math.round(s * 1e7)},${held === null ? "NA" : held},${held === null ? "NA" : 1.5}`;
}

test("PresentMon lives under build/, out of git", () => {
  assert.equal(presentMonPath("/x"), path.join("/x", "build", "PresentMon-2.6.0-x64.exe"));
  assert.match(PRESENTMON_URL, /^https:\/\/github\.com\/GameTechDev\/PresentMon\/releases\/download\/v2\.6\.0\//);
});

test("PresentMon is downloaded once and kept only with Intel's valid signature", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-pm-"));
  const exe = path.join(dir, "build", "pm.exe");
  const calls = [];
  const ok = (cmd, args) => { calls.push(cmd); if (cmd === "curl") fs.writeFileSync(exe, "x"); return { status: 0, stdout: cmd === "curl" ? "" : INTEL }; };
  assert.equal(ensurePresentMon({ exe, run: ok }), exe);
  assert.deepEqual(calls, ["curl", "powershell"]);
  calls.length = 0;
  assert.equal(ensurePresentMon({ exe, run: ok }), exe);
  assert.deepEqual(calls, ["powershell"]);
  assert.throws(() => ensurePresentMon({ exe, run: () => ({ status: 0, stdout: "HashMismatch|CN=Someone" }) }), /not validly signed by Intel \(HashMismatch\)/);
  assert.equal(fs.existsSync(exe), false);
  assert.throws(() => ensurePresentMon({ exe, run: () => ({ status: 0, stdout: "" }), exists: () => true }), /no signature/);
  assert.throws(() => ensurePresentMon({ exe, run: () => ({ status: 6, stderr: "could not resolve host\n" }) }), /download failed: could not resolve host/);
});

test("the admin script runs PresentMon elevated and waits, on one process or every process", () => {
  const all = adminScript("C:\\pm.exe", { out: "C:\\o.csv", seconds: 15 });
  assert.match(all, /Start-Process -FilePath 'C:\\pm\.exe' -Verb RunAs -Wait -ArgumentList @\('--output_file', 'C:\\o\.csv', '--timed', '15',/);
  assert.match(all, /'--qpc_time'/);
  assert.doesNotMatch(all, /--process_name/);
  const one = adminScript("C:\\pm.exe", { out: "C:\\it's.csv", seconds: 5, processName: "forkundera.exe", session: "s" });
  assert.match(one, /'--process_name', 'forkundera\.exe'/);
  assert.match(one, /'C:\\it''s\.csv'/);
  assert.match(one, /'--session_name', 's'/);
});

test("running the script resolves with its exit code, and -1 when it cannot start", async () => {
  const fake = (code, event = "exit") => (cmd, args) => { const p = new EventEmitter(); assert.equal(cmd, "powershell"); assert.ok(args.includes("-File")); setImmediate(() => p.emit(event, code)); return p; };
  assert.equal(await runAdminScript("x.ps1", { start: fake(0) }), 0);
  assert.equal(await runAdminScript("x.ps1", { start: fake(new Error("no"), "error") }), -1);
});

test("the counter frequency is read once, 10 MHz when it cannot be", () => {
  assert.equal(qpcFrequency(() => ({ stdout: "24000000\r\n" })), 24000000);
  assert.equal(qpcFrequency(() => ({ stdout: "" })), 1e7);
});

test("the CSV comes back by column with its byte-order mark gone", () => {
  const r = parsePresentMon(`${HEAD}\n${pm("game.exe", 1, 2.1)}\n`);
  assert.equal(r[0].Application, "game.exe");
  assert.equal(r[0].MsUntilDisplayed, "1.5");
});

test("each app's frames handed over and shown, and the compositor's updates a second", () => {
  const rows = [];
  for (let i = 0; i < 100; i++) rows.push(pm("game.exe", i / 50, i % 10 === 0 ? 20 : null));
  for (let i = 0; i < 6; i++) rows.push(pm(COMPOSITOR, i * 0.3, 300, "Hardware: Legacy Flip"));
  const a = analyzePresentMon(parsePresentMon([HEAD, ...rows].join("\n")));
  const game = a.apps.find((g) => g.app === "game.exe");
  assert.equal(game.handed, 100);
  assert.equal(game.shown, 10);
  assert.equal(Math.round(game.shownPct), 10);
  assert.equal(game.maxHoldMs, 20);
  assert.deepEqual(a.compositor.perSecond, [4, 2]);
  assert.equal(Math.round(a.compositor.gapMs.max), 300);
  assert.equal(analyzePresentMon([]), null);
  assert.equal(analyzePresentMon(parsePresentMon([HEAD, pm("a.exe", 0), pm("a.exe", 1)].join("\n"))).compositor, null);
  assert.equal(analyzePresentMon(parsePresentMon([HEAD, pm(COMPOSITOR, 0, 1)].join("\n"))), null);
  assert.equal(analyzePresentMon(parsePresentMon([HEAD, pm(COMPOSITOR, 0, 1), pm("a.exe", 0.5)].join("\n"))).compositor.gapMs, null);
});

test("the engine's frames line up with what Windows was handed and showed, second by second", () => {
  const pmRows = parsePresentMon([HEAD, pm("game.exe", 10.1, 2), pm("game.exe", 10.5), pm("game.exe", 11.2, 2), pm("other.exe", 10.2, 2)].join("\n"));
  const traceRows = [{ view_at: 10.0e6 }, { view_at: 10.4e6 }, { view_at: 11.1e6 }];
  assert.deepEqual(alignSeconds(pmRows, traceRows, { app: "game.exe" }), [{ second: 0, drawn: 2, handed: 2, shown: 1 }, { second: 1, drawn: 1, handed: 1, shown: 1 }]);
  assert.deepEqual(alignSeconds([], [], { app: "game.exe" }), []);
});

test("the report says how often the compositor updated, and what to do when nothing was seen", () => {
  assert.match(formatPresentMon(null)[0], /fewer than two frames/);
  const rows = [HEAD, pm("game.exe", 0, 2), pm("game.exe", 1.5, 2), pm(COMPOSITOR, 0, 1), pm(COMPOSITOR, 1, 1)];
  const lines = formatPresentMon(analyzePresentMon(parsePresentMon(rows.join("\n"))));
  assert.match(lines[0], /1\.5 s captured/);
  assert.match(lines.join("\n"), /game\.exe .*handed 1\/s, shown 1\/s \(100%\)/);
  assert.match(lines.join("\n"), /the compositor updated the screen 1 times a second/);
  const solo = formatPresentMon(analyzePresentMon(parsePresentMon([HEAD, pm("game.exe", 0, 2), pm("game.exe", 1, 2)].join("\n"))));
  assert.match(solo.join("\n"), /no compositor frames: capture every process/);
  const once = formatPresentMon(analyzePresentMon(parsePresentMon([HEAD, pm(COMPOSITOR, 0, 1), pm("a.exe", 0.5)].join("\n"))));
  assert.doesNotMatch(once.join("\n"), /between its updates/);
});

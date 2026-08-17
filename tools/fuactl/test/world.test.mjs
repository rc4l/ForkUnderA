import { test } from "node:test";
import assert from "node:assert/strict";
import { ensureCheat, verifyWorld, playCvars, sameCvarValue, CHEATS } from "../src/play.mjs";
import { engineArgs } from "../src/launch.mjs";

// A stand-in engine: answers console commands from a script of replies, and records what it was asked.
function fakeEngine(replies) {
  const asked = [];
  return {
    asked,
    exec: async (_c, text) => { asked.push(text); const r = replies[text]; return typeof r === "function" ? r() : (r ?? ""); },
  };
}

test("a cheat that is already ON is left on, not toggled off", () => {
  // [rc4l] This is the bug it exists for. `god` is a toggle: issuing it at a session that already
  // had it on turns it OFF. A capture run ended in a death screen that way, with the fault nowhere
  // in the frame, after two separate steps each helpfully "enabling god mode".
  const eng = fakeEngine({ god: "Degreelessness Mode ON\n" });
  return ensureCheat({}, CHEATS[0], eng).then((r) => {
    assert.equal(r.on, true);
    assert.equal(r.flipped, false);
    assert.deepEqual(eng.asked, ["god"], "one issue is enough when it lands on");
  });
});

test("a cheat that lands OFF is issued again until it is on", () => {
  let n = 0;
  const eng = fakeEngine({ god: () => (++n === 1 ? "Degreelessness Mode OFF\n" : "Degreelessness Mode ON\n") });
  return ensureCheat({}, CHEATS[0], eng).then((r) => {
    assert.equal(r.on, true);
    assert.equal(r.flipped, true);
    assert.deepEqual(eng.asked, ["god", "god"]);
  });
});

test("an engine that says neither is reported as unknown, never assumed on", () => {
  const eng = fakeEngine({ god: "" });
  return ensureCheat({}, CHEATS[0], eng).then((r) => assert.equal(r.on, null));
});

test("the world is read BACK, and a wrong value is reported rather than hoped over", async () => {
  const eng = fakeEngine({
    sv_nomonsters: '"sv_nomonsters" is "true"\n',
    sv_freelook:   '"sv_freelook" is "1"\n',     // the NO bit -- freelook is actually OFF here
    sv_nofreeaim:  '"sv_nofreeaim" is "false"\n',
    autoaim:       '"autoaim" is "0"\n',
  });
  const report = await verifyWorld({}, eng);
  const byName = Object.fromEntries(report.map((r) => [r.name, r]));

  assert.equal(byName.sv_nomonsters.ok, true);
  assert.equal(byName.sv_freelook.ok, false, "1 is DF_NO_FREELOOK; it must not pass as enabled");
  assert.equal(byName.sv_freelook.actual, "1");
  assert.equal(byName.autoaim.ok, true);
});

test("a cvar the engine does not answer is a failure, not a pass", async () => {
  const report = await verifyWorld({}, fakeEngine({}));
  assert.ok(report.every((r) => r.ok === false), "silence must never read as agreement");
});

test("the hands-on build asks for no monsters and for freelook enabled", () => {
  const cv = playCvars({});
  assert.equal(cv.sv_nomonsters, 1);
  assert.equal(cv.sv_freelook, 2, "1 would DISABLE freelook");
  assert.equal(cv.sv_nofreeaim, 0);
  // [rc4l] The BFG reads its own dmflags2 bit in A_FireBFG and ignores sv_nofreeaim entirely, so a
  // build can pass every other free-aim check and still auto-aim the one weapon being tested with.
  assert.equal(cv.sv_bfgfreeaim, 1);
  assert.equal(playCvars({ monsters: true }).sv_nomonsters, undefined);
  assert.equal(playCvars({ sideBySide: true }).fua_dg_embed, 0, "both renderers on screen at once");
});

test("cheats run AFTER the map, cvars BEFORE it", () => {
  // [rc4l] Two opposite ordering requirements on one command line, and getting either backwards
  // fails silently. A cvar after +map does nothing to the level already standing; a cheat before it
  // acts on a player pawn that does not exist yet.
  const args = engineArgs({ map: "MAP01", cvars: { sv_nomonsters: 1 }, postMap: ["god"] }, "x.ini");
  const at = args.indexOf("+map");

  assert.ok(args.indexOf("sv_nomonsters") < at, "a cvar after +map never reaches the level");
  assert.ok(args.indexOf("+god") > at, "a cheat before +map has no pawn to act on");
});

test("true/false and 1/0 are the same answer, because Doom uses both", () => {
  // [rc4l] The very first run of the world check reported sv_nofreeaim as BAD: the expectation said
  // "false", the engine said "0", and the setting was correct all along. A check that cries wolf
  // over its own formatting gets ignored, which costs more than not having it.
  assert.ok(sameCvarValue("0", "false"));
  assert.ok(sameCvarValue("true", "1"));
  assert.ok(sameCvarValue("2", "2"));
  assert.ok(!sameCvarValue("1", "2"), "a genuinely different value must still fail");
  assert.ok(!sameCvarValue(null, "0"), "silence is not agreement");
});

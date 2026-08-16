import test from "node:test";
import assert from "node:assert";
import { makeUndisturbed, warpTo, findOutdoorSpot } from "../src/undisturbed.mjs";

// Fake bridge: records every RPC and replays scripted console output to whoever is listening at the
// moment the command is sent, which is what the real "out" event stream does.
class FakeClient {
  constructor(replies = {}) {
    this.calls = [];
    this.handlers = [];
    this.replies = replies;
    this.result = {};
  }
  onEvent(fn) {
    this.handlers.push(fn);
    return () => { this.handlers = this.handlers.filter((h) => h !== fn); };
  }
  async rpc(cmd, args) {
    this.calls.push({ cmd, args });
    const scripted = this.replies[args && args.text];
    if (scripted) {
      const line = Array.isArray(scripted) ? scripted.shift() : scripted;
      if (line) for (const h of this.handlers) h("out", { text: line });
    }
    return this.result;
  }
  execs() {
    return this.calls.filter((c) => c.cmd === "console.exec").map((c) => c.args.text);
  }
}

const ON = {
  god: "Degreelessness Mode ON",
  notarget: "notarget ON",
  fly: "You feel lighter",
};

test("makes the player unkillable, airborne, and everything else friendly", async () => {
  const c = new FakeClient({ ...ON });
  const applied = await makeUndisturbed(c, { quietMs: 0 });
  assert.deepEqual(c.execs(), ["god", "notarget", "fly", "sv_fua_friendlymonsters 1"]);
  assert.deepEqual(applied, { god: true, notarget: true, fly: true, friendlymonsters: true });
});

test("each protection can be opted out individually", async () => {
  const c = new FakeClient({ ...ON });
  await makeUndisturbed(c, { quietMs: 0, fly: false });
  assert.deepEqual(c.execs(), ["god", "notarget", "sv_fua_friendlymonsters 1"]);

  const d = new FakeClient({ ...ON });
  await makeUndisturbed(d, { quietMs: 0, god: false, notarget: false });
  assert.deepEqual(d.execs(), ["fly"], "the caller asked for the fight, so nothing is pacified");
});

// The cvar is a STATE, not a toggle, which is the whole reason it replaced `notarget`: running the
// tool twice against one instance must not undo the protection it just applied.
test("staying friendly is idempotent across repeated runs", async () => {
  const c = new FakeClient({ ...ON });
  await makeUndisturbed(c, { quietMs: 0 });
  await makeUndisturbed(c, { quietMs: 0 });
  const sets = c.execs().filter((e) => e.startsWith("sv_fua_friendlymonsters"));
  assert.deepEqual(sets, ["sv_fua_friendlymonsters 1", "sv_fua_friendlymonsters 1"]);
});

// The regression this file mostly exists for. These are toggles, so running against an instance that
// is already protected turns the protection OFF -- the second run of a probe would silently measure
// an unprotected player. The engine says which way it went; that answer is used, not assumed.
test("re-arms a cheat that was already on, instead of leaving it toggled off", async () => {
  const c = new FakeClient({
    god: ["Degreelessness Mode OFF", "Degreelessness Mode ON"],
    notarget: "notarget ON",
    fly: "You feel lighter",
  });
  const applied = await makeUndisturbed(c, { quietMs: 0 });
  assert.deepEqual(c.execs(), ["god", "god", "notarget", "fly", "sv_fua_friendlymonsters 1"]);
  assert.equal(applied.god, true);
});

// Caught live: the "off" pattern for fly was guessed rather than read out of language.enu, so an
// already-flying player matched neither branch, fly was left toggled OFF, and the tool said so
// without anyone noticing the destination was now unreachable.
test("re-arms fly using the engine's real gravity message", async () => {
  const c = new FakeClient({
    god: "Degreelessness Mode ON",
    notarget: "notarget ON",
    fly: ["Gravity weighs you down", "You feel lighter"],
  });
  const applied = await makeUndisturbed(c, { quietMs: 0 });
  assert.deepEqual(c.execs(), ["god", "notarget", "fly", "fly", "sv_fua_friendlymonsters 1"]);
  assert.equal(applied.fly, true);
});

test("reports a cheat the engine never acknowledged as not applied", async () => {
  const c = new FakeClient({ notarget: "notarget ON", fly: "You feel lighter" });
  const applied = await makeUndisturbed(c, { quietMs: 0 });
  assert.equal(applied.god, false, "silence means it did not take, e.g. cheats are blocked");
  assert.equal(applied.fly, true);
});

test("warpTo sends absolute z, angle and pitch, and omits what was not asked for", async () => {
  const c = new FakeClient();
  c.result = { x: 10, y: 20, z: 64, angle: 90, pitch: -45, sector: 3 };
  const at = await warpTo(c, 10.4, 20.6, { z: 64, angle: 90, pitch: -45, settleMs: 0 });
  assert.deepEqual(c.calls[0], {
    cmd: "player.setpos",
    args: { x: 10, y: 21, z: 64, angle: 90, pitch: -45 }, // x/y rounded, the rest verbatim
  });
  assert.equal(at.z, 64);

  const d = new FakeClient();
  await warpTo(d, 1, 2, { settleMs: 0 });
  assert.deepEqual(d.calls[0].args, { x: 1, y: 2 }, "no z/angle/pitch keys when unspecified");
});

test("warpTo refuses a destination that is not a pair of numbers", async () => {
  const c = new FakeClient();
  assert.equal(await warpTo(c, undefined, 5, { settleMs: 0 }), null);
  assert.equal(await warpTo(c, NaN, 5, { settleMs: 0 }), null);
  assert.equal(c.calls.length, 0, "nothing sent, so the caller cannot mistake it for a move");
});

test("findOutdoorSpot parses the engine's warp line, and returns null when there is none", async () => {
  const c = new FakeClient({ fua_skytintinfo: "skytint: sectors=463 any=1 outdoors: warp 827 -173" });
  const spot = await findOutdoorSpot(c, { waitMs: 0, enable: false });
  assert.equal(spot.x, 827);
  assert.equal(spot.y, -173, "negative coordinates are common and must survive the parse");

  const d = new FakeClient({ fua_skytintinfo: "skytint: sectors=12 any=0" });
  assert.equal(await findOutdoorSpot(d, { waitMs: 0, enable: false }), null);
});

// The spot comes out of the sky-tint table, so with the feature off the diagnostic truthfully says
// nothing sees sky -- indistinguishable from an indoor level. Doom 2 MAP01 reported "no outdoor
// spot" until the cvar was set.
test("findOutdoorSpot switches the sky tint on before asking", async () => {
  const c = new FakeClient({ fua_skytintinfo: "skytint: outdoors: warp 489 87" });
  await findOutdoorSpot(c, { waitMs: 0 });
  assert.equal(c.execs()[0], "cl_fua_skytint 1");
  assert.equal(c.execs()[1], "fua_skytintinfo");
});

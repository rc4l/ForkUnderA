import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeTraces, buildAcsTracesPk3, waitingScript, loggedStrings, fits, FN, TR, TRF, KIND, ACT, TQ, WANT, WANT_STRINGS, MAP } from "../scenarios/acstraces.mjs";
import { PCD } from "../src/acs.mjs";

const WARN = "fua_TracerCreate: a script already holds 16 tracers; free one with fua_TracerFree.";
const value = (w) => (Array.isArray(w) ? w[0] : w !== null && typeof w === "object" ? w.bits : w);
const line = (tag, values) => `${tag} ${values.map(value).join(" ")}`;
const strings = (tag, values) => `${tag}|${values.join("|")}|`;
const good = () => ({
  ...Object.fromEntries(Object.entries(WANT).map(([tag, want]) => [tag, tag === "l1" ? `${WARN}\n${line(tag, want)}` : line(tag, want)])),
  ...Object.fromEntries(Object.entries(WANT_STRINGS).map(([tag, want]) => [tag, strings(tag, want)])),
});

test("the run passes with every expected value and names each wrong one by index", () => {
  assert.deepEqual(judgeTraces(good()), []);
  assert.match(judgeTraces({ ...good(), w1: line("w1", [0, ...WANT.w1.slice(1)]) }).join(), /w1\[0\] is 0, not 3/);
  assert.match(judgeTraces({ ...good(), f2: "f2|STARTAN2|" }).join(), /f2\[0\] is "STARTAN2", not "COMPBLUE"/);
  assert.match(judgeTraces({ ...good(), v3: "" }).join(), /script v3 logged no strings/);
  assert.match(judgeTraces({ ...good(), n1: "" }).join(), /script n1 logged nothing/);
  assert.match(judgeTraces({ ...good(), l1: line("l1", WANT.l1) }).join(), /warned 0 times/);
});

test("a check fits exactly, within its slack, or by its bits", () => {
  assert.ok(fits(5, 5) && !fits(5, 6));
  assert.ok(fits(10, [12, 2]) && !fits(9, [12, 2]));
  assert.ok(fits(0x08000005, { bits: 0x08000000 }) && !fits(5, { bits: 0x08000000 }));
  assert.deepEqual(loggedStrings("x s2|CEIL3_5|FLOOR4_8|", "s2"), ["CEIL3_5", "FLOOR4_8"]);
  assert.deepEqual(loggedStrings("w2||", "w2"), [""]);
  assert.equal(loggedStrings("nothing", "s2"), null);
});

test("the trace functions, TR_ properties, TRF_ flags and rule constants keep their numbers", () => {
  assert.deepEqual([FN.TracerCreate, FN.LineTrace, FN.TraceResult, FN.SectorInfo], [24050, 24055, 24058, 24061]);
  assert.deepEqual([TR.HitType, TR.HitLine, TR.F3DFloor, TR.HitTexture], [0, 10, 17, 20]);
  assert.deepEqual([TRF.ABSPOSITION, TRF.NOSKY, TRF.SOLIDACTORS, TRF.FUA_PHYSICALSIZE], [1, 64, 256, 1 << 20]);
  assert.deepEqual([FN.TraceRule, FN.Trace, TRF.FUA_ALLLINES], [24053, 24056, 1 << 21]);
  assert.deepEqual([KIND.ANY, KIND.FFLOOR, ACT.STOP, ACT.RECORD, TQ.LINEFLAG, TQ.TEXTURE], [64, 69, 0, 4, 48, 50]);
});

test("the map names the lines and sectors the checks read, and the pk3 holds it", () => {
  const { m, ix } = MAP;
  assert.equal(m.lines[ix.east].id, 7);
  assert.equal(m.lines[ix.blockhitscan].blockhitscan, true);
  assert.equal(m.sectors[ix.step].heightfloor, 64);
  const words = waitingScript();
  assert.ok(words.includes(PCD.IFGOTO) && words.at(-1) === PCD.TERMINATE);
  assert.ok(buildAcsTracesPk3().toString("latin1").includes("maps/MAP01.wad"));
});

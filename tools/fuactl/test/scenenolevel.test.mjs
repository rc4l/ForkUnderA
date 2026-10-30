import { test } from "node:test";
import assert from "node:assert/strict";
import { buildSceneNoLevelWad, sceneCommands, judgeSceneNoLevel, REFUSAL } from "../scenarios/scenenolevel.mjs";
import { readWad } from "../src/udmf.mjs";

// [Doruk-Aksoy] Each scene command's output as a refusal.
const refusedAll = () => sceneCommands("x.png").map((command) => ({ command, out: `${REFUSAL}\n` }));
const PASSING = { crashed: false, alive: true, outs: refusedAll(), wroteOutside: false, wroteInLevel: true };

test("the map holds grid squared rooms and one player start", () => {
  const textmap = readWad(buildSceneNoLevelWad(3)).find((l) => l.name === "TEXTMAP").data.toString("latin1");
  assert.equal(textmap.match(/^sector$/gm).length, 9);
  assert.equal(textmap.match(/^thing$/gm).length, 1);
});

test("the commands are the shot to the named file, the bench and the scale probe", () => {
  assert.deepEqual(sceneCommands("C:/t/a.png"), ['fua_diligent_shot "C:/t/a.png"', "fua_diligent_bench 5", "fua_diligent_scale 2"]);
});

test("every command refused, nothing written outside and a shot in the next level passes", () => {
  assert.deepEqual(judgeSceneNoLevel("MAP01 vk", PASSING),
    { line: "PASS MAP01 vk: 3 scene commands refused outside a level and a shot in the next level wrote its picture", failed: 0 });
});

test("a crash names the command it died at", () => {
  const outs = [{ command: 'fua_diligent_shot "x.png"', out: "" }];
  assert.deepEqual(judgeSceneNoLevel("MAP01 vk", { ...PASSING, crashed: true, outs }),
    { line: 'FAIL MAP01 vk: the engine crashed at fua_diligent_shot "x.png" outside a level', failed: 1 });
});

test("an engine that stopped answering before any command names endgame", () => {
  assert.deepEqual(judgeSceneNoLevel("MAP01 vk", { ...PASSING, alive: false, outs: [] }),
    { line: "FAIL MAP01 vk: the engine stopped answering at endgame outside a level", failed: 1 });
});

test("a command that drew rather than refused fails with what it printed", () => {
  const outs = refusedAll();
  outs[1] = { command: "fua_diligent_bench 5", out: "Diligent bench: 5 frames\n" };
  assert.deepEqual(judgeSceneNoLevel("MAP01 vk", { ...PASSING, outs }),
    { line: 'FAIL MAP01 vk: fua_diligent_bench 5 outside a level printed "Diligent bench: 5 frames" rather than refusing', failed: 1 });
});

test("a picture written outside a level fails", () => {
  assert.equal(judgeSceneNoLevel("MAP01 vk", { ...PASSING, wroteOutside: true }).line, "FAIL MAP01 vk: the shot outside a level wrote a picture");
});

test("no picture from the next level fails", () => {
  assert.equal(judgeSceneNoLevel("MAP01 vk", { ...PASSING, wroteInLevel: false }).line, "FAIL MAP01 vk: a shot in the next level wrote no picture");
});

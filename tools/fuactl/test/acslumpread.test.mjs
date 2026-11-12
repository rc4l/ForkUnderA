import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeBytes, buildScript, buildAcsLumpReadPk3, BYTES, DATA_PATH } from "../scenarios/acslumpread.mjs";
import { ACSF, PCD } from "../src/acs.mjs";
import { readPk3 } from "../src/udmf.mjs";

test("the log passes with the file's bytes unsigned", () => {
  assert.deepEqual(judgeBytes("bytes 200 5"), []);
  assert.deepEqual(judgeBytes("bytes -56 5"), ["the bytes read as -56 5, not 200 5"]);
  assert.deepEqual(judgeBytes("nothing"), ["script 1 logged no bytes (nothing)"]);
});

test("the script reads the handle's bytes into hub array 0, and the pk3 holds them", () => {
  const code = buildScript();
  const at = code.indexOf(ACSF.LumpReadHub);
  assert.deepEqual(code.slice(at - 12, at + 1), [PCD.PUSHMAPVAR, 0, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, BYTES.length, PCD.CALLFUNC, 5, ACSF.LumpReadHub]);
  const file = readPk3(buildAcsLumpReadPk3()).find((f) => f.name === DATA_PATH);
  assert.deepEqual([...file.data], BYTES);
});

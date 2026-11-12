import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeAcsVars, buildNetAcsVarsPk3, MAPS, VAR } from "../scenarios/netacsvars.mjs";

test("the client passes on MAP02 still holding what it stored", () => {
  assert.deepEqual(judgeAcsVars({ map: "MAP02", answer: `acsvar ${VAR.scope} ${VAR.index} = ${VAR.value}` }), []);
  assert.deepEqual(judgeAcsVars({ map: "MAP02", answer: `acsvar ${VAR.scope} ${VAR.index} = 0` }), [`the client's ${VAR.scope} ${VAR.index} is 0 on MAP02, not the ${VAR.value} it stored on MAP01`]);
  assert.deepEqual(judgeAcsVars({ map: "MAP01", answer: "" }), ["the client is on MAP01, not MAP02", "getacsvar gave no value ()"]);
});

test("the pk3 holds both maps", () => {
  const text = buildNetAcsVarsPk3().toString("latin1");
  for (const m of MAPS) assert.ok(text.includes(`maps/${m}.wad`), m);
});

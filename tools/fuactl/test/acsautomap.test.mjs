import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeStep, buildScript, buildAcsAutomapPk3, STEPS, AUTOMAP } from "../scenarios/acsautomap.mjs";
import { ACSF } from "../src/acs.mjs";

test("a step passes when CheckAutomap reads the state wanted", () => {
  assert.deepEqual(judgeStep("automap 2", { toggles: 1, want: "overlay" }), []);
  assert.deepEqual(judgeStep("automap 0", { toggles: 2, want: "full" }), ["after 2 togglemaps CheckAutomap read 0, not full's 1"]);
  assert.deepEqual(judgeStep("", { toggles: 0, want: "off" }), ["after 0 togglemaps script 1 logged nothing ()"]);
});

test("togglemap steps off, overlay, full, off, and the script calls CheckAutomap", () => {
  assert.deepEqual(STEPS.map((s) => AUTOMAP[s.want]), [0, 2, 1, 0]);
  assert.ok(buildScript().includes(ACSF.CheckAutomap));
  assert.ok(buildAcsAutomapPk3().toString("latin1").includes("maps/MAP01.wad"));
});

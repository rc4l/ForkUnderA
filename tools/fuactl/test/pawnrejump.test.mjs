import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScript, buildPawnRejumpPk3, PAWN } from "../scenarios/pawnrejump.mjs";
import { APROP_REJUMPDELAY } from "../scenarios/netrejumpdelay.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("the puke spawns the pawn by the step, sets its RejumpDelay and pushes it up onto it", () => {
  const words = buildScript();
  assert.ok(words.includes(ACSF.SpawnForced) && words.includes(ACSF.SetActorVelocity) && words.includes(ACSF.CheckActorClass));
  assert.ok(words.includes(PAWN.velz * 65536) && words.includes(APROP_REJUMPDELAY) && words.includes(PCD.SETACTORPROPERTY));
  assert.ok(buildPawnRejumpPk3().toString("latin1").includes("maps/MAP01.wad"));
});

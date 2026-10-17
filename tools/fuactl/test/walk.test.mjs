import { test } from "node:test";
import assert from "node:assert/strict";
import { parseViewTraceMoves, walkCheck } from "../src/trace.mjs";

test("a view trace's report gives its back steps and lurches", () => {
  assert.deepEqual(parseViewTraceMoves("fua_viewtrace over 10 frames\n  2 frames stepped back against the way the view was moving, 3 lurched ahead of its steady speed\n"), { back: 2, lurch: 3 });
  assert.equal(parseViewTraceMoves("Unknown command"), null);
});

test("a walk check holds W for each run, traces it and lets go", async () => {
  const said = [];
  const sent = [];
  const c = { rpc: async (cmd, args) => { sent.push(`${cmd} ${args.evtype}`); } };
  const exec = async (_, cmd) => { said.push(cmd); return cmd === "fua_viewtrace end" ? "0 frames stepped back against the way the view was moving, 1 lurched" : ""; };
  const stood = [];
  const r = await walkCheck(c, { runs: 2, seconds: 0, start: { x: 1 }, exec, standAt: async (_, at) => stood.push(at), wait: async () => {} });
  assert.deepEqual(r, [{ back: 0, lurch: 1 }, { back: 0, lurch: 1 }]);
  assert.deepEqual(sent, ["input.event 1", "input.event 2", "input.event 1", "input.event 2"]);
  assert.equal(stood.length, 2);
  assert.equal(said[0], "bind w +forward");
});

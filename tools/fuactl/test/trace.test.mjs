import { test } from "node:test";
import assert from "node:assert/strict";
import os from "node:os";
import path from "node:path";
import { parseViewTrace, runsOf, analyzeViewTrace, formatViewTrace, recordViewTrace, TIC_WAIT_US } from "../src/trace.mjs";

const HEAD = "us,deg,tic,local,view_at,present_at,frame_us,sim,walk,rebake,actors,backend,upload,lights,present,input,display,flip,mouse_msgs,mouse_ignored,mouse_dx,mouse_dy,mouse_to_view,mouse_kept,grabbed,active,paused,menu,console";

// [rc4l] One CSV row from the columns that matter here, the rest zero.
function row(o) {
  const r = { us: 0, deg: 0, tic: 0, local: 0, view_at: 0, present_at: 0, frame_us: 1000, sim: 50, walk: 600, rebake: 0, actors: 0, backend: 200, upload: 0, lights: 0,
    present: 50, input: 10, display: 900, flip: 250, mouse_msgs: 1, mouse_ignored: 0, mouse_dx: 1, mouse_dy: 0, mouse_to_view: 1, mouse_kept: 0,
    grabbed: 1, active: 1, paused: 0, menu: 0, console: 0, ...o };
  return HEAD.split(",").map((c) => r[c]).join(",");
}

// [rc4l] Rows a frame apart from `us`, each `frame_us` long.
function rowsFrom(list) {
  let us = 0;
  return list.map((o) => { const line = row({ ...o, us, view_at: 5000000 + us, deg: o.deg ?? 0.1 }); us += o.frame_us ?? 1000; return line; });
}

test("the CSV comes back as rows of numbers by column", () => {
  const r = parseViewTrace(`${HEAD}\n${row({ us: 5, walk: 700 })}\n`);
  assert.equal(r.length, 1);
  assert.equal(r[0].us, 5);
  assert.equal(r[0].walk, 700);
  assert.equal(r[0].console, 0);
  assert.deepEqual(parseViewTrace(""), []);
});

test("runs are found with when they began and how long they lasted, the last one by its own frame", () => {
  const rows = parseViewTrace([HEAD, ...rowsFrom([{}, { sim: 28000, frame_us: 28000 }, { sim: 28000, frame_us: 28000 }, {}, { sim: 28000, frame_us: 28000 }])].join("\n"));
  const runs = runsOf(rows, (r) => r.sim >= TIC_WAIT_US);
  assert.deepEqual(runs.map((r) => [r.rows, r.ms]), [[2, 56], [1, 28]]);
  assert.equal(runs[0].atS, 0.001);
  assert.deepEqual(runsOf(rows, (r) => r.sim >= TIC_WAIT_US, 50).map((r) => r.rows), [2]);
});

test("frames are judged apart from tic waits, and the slow ones show what they spent more on", () => {
  const list = [...Array(20).fill({}), { frame_us: 3000, walk: 2500, display: 2900 }, { sim: 28000, frame_us: 28000, console: 1, active: 1 }];
  const a = analyzeViewTrace(parseViewTrace([HEAD, ...rowsFrom(list)].join("\n")));
  assert.equal(a.frames, 22);
  assert.equal(a.frameMs.p50, 1);
  assert.equal(a.frameMs.max, 3);
  assert.equal(a.slow.frames, 1);
  assert.equal(a.slow.overMs, 2);
  assert.equal(a.slow.phases.walk - a.slow.restPhases.walk, 1900);
  assert.equal(a.outsideUs, 40);
  assert.deepEqual(a.ticWaits.map((w) => w.rows), [1]);
  assert.deepEqual(a.states, { playing: 21, console: 1 });
});

test("mouse input is counted, and gaps while playing and a view that stood still while the mouse moved are found", () => {
  const list = [...Array(5).fill({}), ...Array(15).fill({ mouse_msgs: 0, mouse_dx: 0, frame_us: 10000 }), ...Array(5).fill({}),
    ...Array(15).fill({ deg: 0, frame_us: 10000 }), { menu: 1, mouse_msgs: 0, mouse_kept: 3 }, {}];
  const a = analyzeViewTrace(parseViewTrace([HEAD, ...rowsFrom(list)].join("\n")));
  assert.deepEqual(a.mouse.gaps.map((g) => [g.rows, g.ms]), [[15, 150]]);
  assert.deepEqual(a.mouse.stillWhileMoving.map((g) => g.rows), [15]);
  assert.equal(a.mouse.keptFromView, 3);
  assert.equal(a.mouse.messages, 26);
  assert.equal(a.states.menu, 1);
});

test("a recording with too few frames says so, and the report reads as lines", () => {
  assert.equal(analyzeViewTrace(parseViewTrace(`${HEAD}\n${row({})}`)), null);
  assert.match(formatViewTrace(null)[0], /too few frames/);
  const list = [...Array(20).fill({}), { frame_us: 3000, walk: 2500 }, { sim: 28000, frame_us: 28000, paused: 1 },
    ...Array(120).fill({ mouse_msgs: 0, mouse_dx: 0 }), {}, ...Array(120).fill({ deg: 0 })];
  const lines = formatViewTrace(analyzeViewTrace(parseViewTrace([HEAD, ...rowsFrom(list)].join("\n"))));
  assert.match(lines[0], /frames over .* s .*p50 1\.00/);
  assert.match(lines[2], /1 frames over 2\.00 ms spent more on: walk \+1900/);
  assert.match(lines.join("\n"), /waiting for a tic .*for 0\.0 s/);
  assert.match(lines.join("\n"), /no mouse input while playing/);
  assert.match(lines.join("\n"), /the mouse moved but the view did not/);
  const calm = formatViewTrace(analyzeViewTrace(parseViewTrace([HEAD, ...rowsFrom(Array(10).fill({}))].join("\n"))));
  assert.match(calm.join("\n"), /no frame took twice the median/);
  assert.match(calm.join("\n"), /never waiting for a tic/);
});

test("more than a dozen mouse gaps are listed as a dozen and a count", () => {
  const list = [];
  for (let i = 0; i < 14; i++) list.push(...Array(12).fill({ mouse_msgs: 0, mouse_dx: 0, frame_us: 10000 }), {});
  const lines = formatViewTrace(analyzeViewTrace(parseViewTrace([HEAD, ...rowsFrom(list)].join("\n"))));
  assert.match(lines.join("\n"), /and 2 more/);
});

test("recording arms the trace, ends it after the wait even when the wait fails, and reads the file back", async () => {
  const out = path.join(os.tmpdir(), "fuactl-trace-test.csv");
  const sent = [];
  const exec = async (c, cmd) => { sent.push(cmd); };
  const rows = await recordViewTrace({}, { out, exec, during: async () => {}, wait: async () => {}, exists: () => true, read: () => `${HEAD}\n${row({ us: 9 })}` });
  assert.deepEqual(sent, [`fua_viewtrace 300000 "${out}"`, "fua_viewtrace end"]);
  assert.equal(rows[0].us, 9);
  sent.length = 0;
  await assert.rejects(recordViewTrace({}, { out, exec, during: async () => { throw new Error("declined"); }, wait: async () => {}, exists: () => true, read: () => "" }), /declined/);
  assert.equal(sent[1], "fua_viewtrace end");
  await assert.rejects(recordViewTrace({}, { out, exec, seconds: 0, wait: async () => {}, exists: () => false }), /wrote nothing/);
});

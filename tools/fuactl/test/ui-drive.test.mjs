import { test } from "node:test";
import assert from "node:assert/strict";
import {
  menuNav, click, rightClick, middleClick, drag, wheel, typeText,
  padButton, padDpad, look, stick, stickHold, stickClear, readMenu, findLabel, clickLabel, screenshot, verifyMenu,
} from "../src/ui.mjs";

// A stand-in for BridgeClient: records every rpc(), and (for readMenu) replays a dumphud capture
// through the onEvent channel so the driving helpers can be exercised with no engine or sockets.
function mockClient({ hud = "" } = {}) {
  const calls = [];
  let handler = null;
  return {
    calls,
    rpc: async (cmd, args) => {
      calls.push({ cmd, args });
      if (cmd === "console.exec" && args && args.text === "dumphud" && handler) handler("out", { text: hud });
      return { ok: true };
    },
    onEvent: (fn) => { handler = fn; return () => { handler = null; }; },
    events: () => calls.filter((c) => c.cmd === "input.event").map((c) => c.args),
  };
}

test("menuNav posts a paired keydown/keyup per step", async () => {
  const c = mockClient();
  await menuNav(c, ["down", "enter"], { delay: 0 });
  const ev = c.events();
  assert.deepEqual(ev.map((e) => e.subtype), [1, 3, 1, 3]); // down↓ down↑ enter↓ enter↑
  assert.deepEqual(ev.map((e) => e.data1), [10, 10, 13, 13]);
  await assert.rejects(() => menuNav(c, ["pageup"], { delay: 0 })); // unknown nav key
});

test("mouse helpers emit hover + button down/up with the right subtypes", async () => {
  const c = mockClient();
  await click(c, 493, 260, { delay: 0 });
  let ev = c.events();
  assert.deepEqual(ev.map((e) => e.subtype), [6, 7, 8]); // move, Ldown, Lup
  assert.equal(ev[1].data1, 493); assert.equal(ev[1].data2, 260);

  c.calls.length = 0;
  await rightClick(c, 1, 2, { delay: 0 });
  assert.deepEqual(c.events().map((e) => e.subtype), [6, 13, 14]); // move, Rdown, Rup

  c.calls.length = 0;
  await middleClick(c, 1, 2, { delay: 0 });
  assert.deepEqual(c.events().map((e) => e.subtype), [6, 10, 11]); // move, Mdown, Mup
});

test("drag holds the button across stepped moves then releases", async () => {
  const c = mockClient();
  await drag(c, 0, 0, 80, 0, { steps: 4, delay: 0 });
  const subs = c.events().map((e) => e.subtype);
  assert.equal(subs[0], 6);            // initial move
  assert.equal(subs[1], 7);            // Ldown
  assert.equal(subs[subs.length - 1], 8); // Lup at the end
  assert.equal(subs.filter((s) => s === 6).length, 5); // initial + 4 steps
});

test("wheel and typeText and controller helpers post the expected events", async () => {
  const c = mockClient();
  await wheel(c, 0, 0, "up", 2);
  assert.deepEqual(c.events().map((e) => e.subtype), [16, 16]);

  c.calls.length = 0;
  await typeText(c, "Hi", { delay: 0 });
  assert.deepEqual(c.events().map((e) => e.data1), ["H".charCodeAt(0), "i".charCodeAt(0)]);

  c.calls.length = 0;
  await padButton(c, 1, { hold: 0 });
  assert.deepEqual(c.events().map((e) => [e.evtype, e.data1]), [[1, 0x108], [2, 0x108]]);
  c.calls.length = 0;
  await padDpad(c, "up", { hold: 0 });
  assert.deepEqual(c.events().map((e) => [e.evtype, e.data1]), [[1, 0x188], [2, 0x188]]);
});

test("look and stick call the right RPCs", async () => {
  const c = mockClient();
  await look(c, { yaw: 135, pitch: 10 });
  assert.deepEqual(c.calls[0], { cmd: "input.look", args: { yaw: 135, pitch: 10 } });

  c.calls.length = 0;
  await stick(c, { forward: 1 });
  assert.deepEqual(c.calls[0], { cmd: "input.axis", args: { forward: 1 } });
  await stickClear(c);
  assert.deepEqual(c.calls[1], { cmd: "input.axis", args: { clear: true } });
  await stickHold(c, { side: -1 }, { hold: 0 });
  assert.equal(c.calls[2].args.side, -1);
  assert.deepEqual(c.calls[3].args, { clear: true }); // released after the hold
});

test("readMenu runs dumphud and returns parsed lines; findLabel finds a fragment", async () => {
  const hud = "MCP_HUD\ntext 396 247 56 Vanilla\ntext 493 260 96 Complex Doom\n";
  const c = mockClient({ hud });
  const m = await readMenu(c, { settle: 5 });
  assert.equal(c.calls[0].args.text, "dumphud");
  assert.ok(m.lines.some((l) => l.text === "Complex Doom"));
  const hit = await findLabel(c, "complex doom", { settle: 5 });
  assert.deepEqual(hit, { x: 493, y: 260, w: 96, text: "Complex Doom", cx: 541 });
});

test("clickLabel clicks the label centre (cx), not its text-start", async () => {
  const hud = "MCP_HUD\ntext 100 200 80 Popular Co-op Maps\n";
  const c = mockClient({ hud });
  const hit = await clickLabel(c, "Popular", { delay: 0 });
  assert.equal(hit.cx, 140); // 100 + 80/2
  const move = c.events().find((e) => e.subtype === 6); // the hover move
  assert.equal(move.data1, 140); // clicked the centre, not 100
});

test("screenshot polls while the PNG is absent, then throws if it never lands", async () => {
  const os = await import("node:os");
  const path = await import("node:path");
  const engineBin = path.join(os.tmpdir(), "fuactl-none", "forkundera"); // dir that has no such png
  await assert.rejects(
    () => screenshot({ rpc: async () => ({}), onEvent: () => () => {} }, engineBin, "never_appears", { tries: 3, delay: 1 }),
    /screenshot did not appear/);
});

test("verifyMenu opens, navigates, and screenshots", async () => {
  const fs = await import("node:fs");
  const os = await import("node:os");
  const path = await import("node:path");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-vm-"));
  const engineBin = path.join(dir, "forkundera");
  const calls = [];
  const c = {
    rpc: async (cmd, args) => { calls.push({ cmd, args }); if (cmd === "console.exec" && String(args.text).startsWith("screenshot")) fs.writeFileSync(path.join(dir, "fuactl_verify.png"), "V"); return {}; },
    onEvent: () => () => {},
  };
  const s = await verifyMenu(c, engineBin, { open: "menu_main", steps: ["down"] });
  assert.equal(calls[0].args.text, "menu_main");
  assert.ok(calls.some((x) => x.cmd === "input.event")); // the nav step
  assert.equal(Buffer.from(s.base64, "base64").toString(), "V");
  fs.rmSync(dir, { recursive: true, force: true });
});

test("screenshot execs the CCMD and reads the PNG back as base64", async (t) => {
  const fs = await import("node:fs");
  const os = await import("node:os");
  const path = await import("node:path");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-shot-"));
  const engineBin = path.join(dir, "forkundera");
  const c = {
    calls: [],
    rpc: async (cmd, args) => {
      c.calls.push({ cmd, args });
      if (cmd === "console.exec") fs.writeFileSync(path.join(dir, "shotX.png"), Buffer.from("PNGDATA"));
      return {};
    },
    onEvent: () => () => {},
  };
  const s = await screenshot(c, engineBin, "shotX");
  assert.equal(c.calls[0].args.text, "screenshot shotX");
  assert.equal(Buffer.from(s.base64, "base64").toString(), "PNGDATA");
  fs.rmSync(dir, { recursive: true, force: true });
});

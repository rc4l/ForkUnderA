// Minimal MCP stdio server frontend. Same engine bridge, agent-facing: newline-delimited JSON-RPC 2.0
// over stdin/stdout. Exposes the fuactl capabilities as MCP tools so an LLM agent can drive the
// programmable engine (launch instances, run the determinism check, reap, raw RPC).
import readline from "node:readline";
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runGlTimers } from "./session.mjs";
import { launchInstance, resolveEngine } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { menuNav, click, clickLabel, typeText, screenshot, padButton, padDpad, look, readMenu, findLabel } from "./ui.mjs";

// Run fn with a short-lived, connected+greeted client, then close it.
async function withClient(port, token, fn) {
  const c = new BridgeClient();
  await c.connect(port, { token: token || null });
  await c.waitHello();
  try { return await fn(c); } finally { c.close(); }
}

const TOOLS = [
  { name: "list_instances", description: "List registered engine instances (pid/port).",
    inputSchema: { type: "object", properties: {} } },
  { name: "reap", description: "Prune dead instances; with kill, SIGTERM only orphans (safe for other sessions); with all, every live instance.",
    inputSchema: { type: "object", properties: { kill: { type: "boolean" }, all: { type: "boolean" } } } },
  { name: "launch_instance", description: "Launch one supervised bridge-enabled engine instance.",
    inputSchema: { type: "object", properties: { map: { type: "string" }, seed: { type: "number" } } } },
  { name: "session_check", description: "Run the determinism + desync check across N instances.",
    inputSchema: { type: "object", properties: {
      instances: { type: "number" }, seed: { type: "number" }, map: { type: "string" }, tics: { type: "number" } } } },
  { name: "perf_ablation", description: "Deterministic perf ablation: baseline vs a perturbation, causal frametime delta + sim/render (CPU/GPU) verdict.",
    inputSchema: { type: "object", properties: {
      seed: { type: "number" }, map: { type: "string" }, spawn: { type: "string" }, count: { type: "number" }, frames: { type: "number" } } } },
  { name: "gl_timers", description: "GPU render profiling: arm a gl.timers capture on a running instance and return per-pass GPU milliseconds (scene/translucent/hud2d), whole-frame total, and draw counters. Answers 'which GL pass is eating the frame on this laggy map'. Includes renderer.info (whether timer queries even work on this driver).",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, frames: { type: "number" }, warmup: { type: "number" } } } },
  { name: "renderer_info", description: "Renderer identity (vendor/renderer/GL version) + whether GL timer queries are usable on this driver, so you know if gl_timers will return real numbers.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" } } } },
  { name: "rpc", description: "Send one raw RPC to an instance and return the result.",
    inputSchema: { type: "object", required: ["port", "cmd"], properties: {
      port: { type: "number" }, token: { type: "string" }, cmd: { type: "string" }, args: { type: "object" } } } },
  // --- UI / input layer (ui.mjs) ---
  { name: "ui_menu_nav", description: "Drive a menu with keyboard nav keys (each down is auto-paired with its up).",
    inputSchema: { type: "object", required: ["port", "steps"], properties: {
      port: { type: "number" }, token: { type: "string" },
      steps: { type: "array", items: { type: "string", enum: ["up", "down", "left", "right", "enter", "back", "backspace"] } } } } },
  { name: "ui_click", description: "Mouse click. Give `label` to click an on-screen label's centre (robust), or x,y for a point. button: left/middle/right; double for dbl-click.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, label: { type: "string" }, x: { type: "number" }, y: { type: "number" },
      button: { type: "string", enum: ["left", "middle", "right"] }, double: { type: "boolean" } } } },
  { name: "ui_type", description: "Type text as GUI char events (menu name fields, console).",
    inputSchema: { type: "object", required: ["port", "text"], properties: {
      port: { type: "number" }, token: { type: "string" }, text: { type: "string" } } } },
  { name: "ui_screenshot", description: "Capture the current frame to a PNG and return its path + base64.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, name: { type: "string" } } } },
  { name: "ui_pad_button", description: "Press a controller button (index 1..8) or D-pad direction, held then released.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, index: { type: "number" },
      dpad: { type: "string", enum: ["up", "down", "left", "right"] }, hold: { type: "number" } } } },
  { name: "ui_stick", description: "Hold analog stick axes (yaw/pitch/forward/side/up in [-1,1]); clear:true releases. Drives the ticcmd each tic.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" },
      yaw: { type: "number" }, pitch: { type: "number" }, forward: { type: "number" }, side: { type: "number" }, up: { type: "number" },
      clear: { type: "boolean" } } } },
  { name: "ui_look", description: "Precise relative view rotation in DEGREES (yaw>0 left, pitch>0 down). Exact angular turn, unlike the rate-based stick. Applies next tic.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, yaw: { type: "number" }, pitch: { type: "number" } } } },
  { name: "ui_read", description: "Read the current menu/HUD as structured text (labels + coords) instead of a screenshot -- navigate by label, not pixels.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, full: { type: "boolean" } } } },
  { name: "ui_find", description: "Find an on-screen label (case-insensitive substring) and return its {x,y,text}, or null.",
    inputSchema: { type: "object", required: ["port", "label"], properties: {
      port: { type: "number" }, token: { type: "string" }, label: { type: "string" } } } },
  { name: "world_sectors", description: "Query the level's sectors with specials/damage; damaging:true narrows to floors that hurt. Each row has a guaranteed-interior x,y to warp to.",
    inputSchema: { type: "object", required: ["port"], properties: {
      port: { type: "number" }, token: { type: "string" }, damaging: { type: "boolean" }, limit: { type: "number" } } } },
  { name: "player_setpos", description: "Teleport the console player (single-player instances only). z, angle and pitch are optional and absolute; without z the pawn snaps to the floor. Returns where it actually landed, which differs from the request when z is clamped into the gap the pawn fits in. Prefer this over the `warp` console command, which is x/y only and always floor-snaps.",
    inputSchema: { type: "object", required: ["port", "x", "y"], properties: {
      port: { type: "number" }, token: { type: "string" }, x: { type: "number" }, y: { type: "number" },
      z: { type: "number", description: "Absolute height in map units; omit to drop to the floor." },
      angle: { type: "number", description: "Absolute facing in degrees (0 = east), wrapped into 0-360." },
      pitch: { type: "number", description: "Absolute view pitch in degrees, clamped to +-90. POSITIVE LOOKS DOWN; use -90 to look straight up at the sky." } } } },
];

async function callTool(name, a = {}) {
  switch (name) {
    case "list_instances": return readRegistry().map((e) => ({ pid: e.pid, port: e.port, ppid: e.ppid }));
    case "reap": { const r = reap({ kill: !!a.kill, all: !!a.all }); return { orphans: r.orphan.length, owned: r.owned.length, killed: r.killed.length, pruned: r.prunedCount }; }
    case "launch_instance": { const i = await launchInstance({ map: a.map, seed: a.seed }); return { pid: i.pid, port: i.port, token: i.token }; }
    case "session_check": return runDeterminismCheck({ instances: a.instances, seed: a.seed, map: a.map, tics: a.tics });
    case "perf_ablation": return runPerfAblation({ seed: a.seed, map: a.map, spawn: a.spawn, count: a.count, frames: a.frames });
    case "gl_timers": return runGlTimers({ port: a.port, token: a.token, frames: a.frames, warmup: a.warmup });
    case "renderer_info": return withClient(a.port, a.token, (c) => c.rpc("renderer.info"));
    case "rpc": return withClient(a.port, a.token, (c) => c.rpc(a.cmd, a.args));
    case "ui_menu_nav": return withClient(a.port, a.token, async (c) => { await menuNav(c, a.steps); return { navigated: a.steps }; });
    case "ui_click": return withClient(a.port, a.token, async (c) => {
      if (a.label) return clickLabel(c, a.label, { button: a.button || "left", double: !!a.double });
      await click(c, a.x, a.y, { button: a.button || "left", double: !!a.double }); return { clicked: { x: a.x, y: a.y, button: a.button || "left" } };
    });
    case "ui_type": return withClient(a.port, a.token, async (c) => { await typeText(c, a.text); return { typed: a.text.length }; });
    case "ui_screenshot": return withClient(a.port, a.token, (c) => screenshot(c, resolveEngine(), a.name || "fuactl_shot"));
    case "ui_pad_button": return withClient(a.port, a.token, async (c) => {
      if (a.dpad) { await padDpad(c, a.dpad, { hold: a.hold }); return { dpad: a.dpad }; }
      await padButton(c, a.index, { hold: a.hold }); return { button: a.index };
    });
    case "ui_stick": return withClient(a.port, a.token, (c) => c.rpc("input.axis", a.clear ? { clear: true } : { yaw: a.yaw, pitch: a.pitch, forward: a.forward, side: a.side, up: a.up }));
    case "ui_look": return withClient(a.port, a.token, (c) => look(c, { yaw: a.yaw, pitch: a.pitch }));
    case "ui_read": return withClient(a.port, a.token, async (c) => { const m = await readMenu(c); return a.full ? m : { lines: m.lines.map((l) => l.text) }; });
    case "ui_find": return withClient(a.port, a.token, (c) => findLabel(c, a.label));
    case "world_sectors": return withClient(a.port, a.token, (c) => c.rpc("world.sectors", { damaging: a.damaging ? 1 : 0, limit: a.limit ?? 64 }));
    case "player_setpos": return withClient(a.port, a.token, (c) => c.rpc("player.setpos", { x: a.x, y: a.y }));
    default: throw new Error(`unknown tool: ${name}`);
  }
}

export async function runMcpServer() {
  const rl = readline.createInterface({ input: process.stdin });
  const send = (obj) => process.stdout.write(JSON.stringify(obj) + "\n");
  const reply = (id, result) => send({ jsonrpc: "2.0", id, result });
  const fail = (id, code, message) => send({ jsonrpc: "2.0", id, error: { code, message } });

  for await (const line of rl) {
    const s = line.trim();
    if (!s) continue;
    let msg;
    try { msg = JSON.parse(s); } catch { continue; }
    const { id, method, params } = msg;
    try {
      if (method === "initialize") {
        reply(id, {
          protocolVersion: "2024-11-05",
          capabilities: { tools: {} },
          serverInfo: { name: "fuactl", version: "0.1.0" },
        });
      } else if (method === "notifications/initialized" || method === "initialized") {
        // notification: no response
      } else if (method === "tools/list") {
        reply(id, { tools: TOOLS });
      } else if (method === "tools/call") {
        const out = await callTool(params?.name, params?.arguments || {});
        reply(id, { content: [{ type: "text", text: JSON.stringify(out) }] });
      } else if (method === "ping") {
        reply(id, {});
      } else if (id != null) {
        fail(id, -32601, `method not found: ${method}`);
      }
    } catch (e) {
      if (id != null) fail(id, -32000, e.message);
    }
  }
}

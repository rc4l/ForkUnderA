// Minimal MCP stdio server frontend. Same engine bridge, agent-facing: newline-delimited JSON-RPC 2.0
// over stdin/stdout. Exposes the fuactl capabilities as MCP tools so an LLM agent can drive the
// programmable engine (launch instances, run the determinism check, reap, raw RPC).
import readline from "node:readline";
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck } from "./session.mjs";
import { launchInstance } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";

const TOOLS = [
  { name: "list_instances", description: "List registered engine instances (pid/port).",
    inputSchema: { type: "object", properties: {} } },
  { name: "reap", description: "Prune dead instances; optionally SIGTERM live ones (clean quit).",
    inputSchema: { type: "object", properties: { kill: { type: "boolean" } } } },
  { name: "launch_instance", description: "Launch one supervised bridge-enabled engine instance.",
    inputSchema: { type: "object", properties: { map: { type: "string" }, seed: { type: "number" } } } },
  { name: "session_check", description: "Run the determinism + desync check across N instances.",
    inputSchema: { type: "object", properties: {
      instances: { type: "number" }, seed: { type: "number" }, map: { type: "string" }, tics: { type: "number" } } } },
  { name: "rpc", description: "Send one raw RPC to an instance and return the result.",
    inputSchema: { type: "object", required: ["port", "cmd"], properties: {
      port: { type: "number" }, token: { type: "string" }, cmd: { type: "string" }, args: { type: "object" } } } },
];

async function callTool(name, a = {}) {
  switch (name) {
    case "list_instances": return readRegistry().map((e) => ({ pid: e.pid, port: e.port, ppid: e.ppid }));
    case "reap": { const r = reap({ kill: !!a.kill }); return { live: r.live.length, killed: r.killed.length, pruned: r.prunedCount }; }
    case "launch_instance": { const i = await launchInstance({ map: a.map, seed: a.seed }); return { pid: i.pid, port: i.port, token: i.token }; }
    case "session_check": return runDeterminismCheck({ instances: a.instances, seed: a.seed, map: a.map, tics: a.tics });
    case "rpc": {
      const c = new BridgeClient();
      await c.connect(a.port, { token: a.token || null });
      await c.waitHello();
      const res = await c.rpc(a.cmd, a.args);
      c.close();
      return res;
    }
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

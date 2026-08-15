// BridgeClient: an RPC/event connection to a running engine's native bridge (loopback NDJSON v2).
import net from "node:net";
import { drainLines, frameRequest, classify } from "./proto.mjs";

export class BridgeClient {
  constructor() {
    this.sock = null;
    this.buf = "";
    this.nextId = 1;
    this.pending = new Map();      // id -> {resolve, reject}
    this.eventHandlers = [];       // (event, data) => void
    this.hello = null;
    this._helloWaiters = [];
  }

  connect(port, { host = "127.0.0.1", token = null, timeoutMs = 8000 } = {}) {
    return new Promise((resolve, reject) => {
      const to = setTimeout(() => reject(new Error(`connect timeout on ${host}:${port}`)), timeoutMs);
      this.sock = net.createConnection({ host, port }, () => {
        clearTimeout(to);
        if (token) this.sock.write(frameRequest(0, "auth", { token }));
        resolve(this);
      });
      this.sock.on("error", (e) => { clearTimeout(to); reject(e); });
      this.sock.setEncoding("utf8");
      this.sock.on("data", (chunk) => this._onData(chunk));
      this.sock.on("close", () => {
        for (const { reject: rj } of this.pending.values()) rj(new Error("bridge closed"));
        this.pending.clear();
      });
    });
  }

  _onData(chunk) {
    this.buf += chunk;
    const { messages, rest } = drainLines(this.buf);
    this.buf = rest;
    for (const msg of messages) {
      const kind = classify(msg);
      if (kind === "hello") {
        this.hello = msg;
        for (const w of this._helloWaiters) w(msg);
        this._helloWaiters = [];
      } else if (kind === "event") {
        for (const h of this.eventHandlers) h(msg.event, msg.data || {});
      } else if (kind === "response") {
        const p = this.pending.get(msg.id);
        if (p) {
          this.pending.delete(msg.id);
          if (msg.ok) p.resolve(msg.result);
          else p.reject(new Error(msg.error || "rpc error"));
        }
      }
    }
  }

  waitHello(timeoutMs = 8000) {
    if (this.hello) return Promise.resolve(this.hello);
    return new Promise((resolve, reject) => {
      const to = setTimeout(() => reject(new Error("hello timeout")), timeoutMs);
      this._helloWaiters.push((m) => { clearTimeout(to); resolve(m); });
    });
  }

  onEvent(fn) { this.eventHandlers.push(fn); return () => { const i = this.eventHandlers.indexOf(fn); if (i >= 0) this.eventHandlers.splice(i, 1); }; }

  rpc(cmd, args, timeoutMs = 8000) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const to = setTimeout(() => { this.pending.delete(id); reject(new Error(`rpc timeout: ${cmd}`)); }, timeoutMs);
      this.pending.set(id, {
        resolve: (r) => { clearTimeout(to); resolve(r); },
        reject: (e) => { clearTimeout(to); reject(e); },
      });
      this.sock.write(frameRequest(id, cmd, args));
    });
  }

  // Wait for a specific event once.
  waitEvent(name, timeoutMs = 15000) {
    return new Promise((resolve, reject) => {
      const to = setTimeout(() => reject(new Error(`event timeout: ${name}`)), timeoutMs);
      const h = (event, data) => {
        if (event === name) {
          clearTimeout(to);
          this.eventHandlers = this.eventHandlers.filter((x) => x !== h);
          resolve(data);
        }
      };
      this.eventHandlers.push(h);
    });
  }

  close() { if (this.sock) this.sock.destroy(); }
}

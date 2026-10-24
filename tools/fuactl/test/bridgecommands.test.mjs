import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const RPC = path.join(here, "..", "..", "..", "src", "zandronum", "src", "mcp_rpc.cpp");

// [rc4l] The commands mcp_rpc.cpp's dispatch chain answers.
export function dispatchedCommands(source) {
  return new Set([...source.matchAll(/cmd\s*==\s*"([^"]+)"/g)].map((m) => m[1]));
}

// [rc4l] The commands its capabilities reply advertises, read from the string literals it concatenates.
export function advertisedCommands(source) {
  const start = source.indexOf('cmd == "capabilities"');
  const end = source.indexOf("else if", start + 1);
  const literal = [...source.slice(start, end).matchAll(/"((?:[^"\\]|\\.)*)"/g)].slice(1).map((m) => m[1]).join("").replace(/\\"/g, '"');
  const list = /"commands":\[(.*?)\]/.exec(literal);
  return new Set(list ? [...list[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]) : []);
}

test("the bridge advertises every command it answers and none it does not", () => {
  const source = fs.readFileSync(RPC, "utf8");
  const dispatched = dispatchedCommands(source), advertised = advertisedCommands(source);
  assert.ok(dispatched.size > 20, "the dispatch chain was not found");
  assert.deepEqual([...dispatched].filter((c) => !advertised.has(c)).sort(), [], "answered but not advertised");
  assert.deepEqual([...advertised].filter((c) => !dispatched.has(c)).sort(), [], "advertised but not answered");
});

test("the readers pick the names out of a dispatch chain and a capabilities reply", () => {
  const source = 'if ( cmd == "ping" ) {} else if ( cmd == "capabilities" ) { SendOk( id, "{\\"commands\\":[" "\\"ping\\",\\"capabilities\\"" "]}" ); } else if ( cmd == "x" ) {}';
  assert.deepEqual([...dispatchedCommands(source)].sort(), ["capabilities", "ping", "x"]);
  assert.deepEqual([...advertisedCommands(source)].sort(), ["capabilities", "ping"]);
});

// [rc4l] fua_netcap captures (every command a server wrote for a client, a line each) read, named and compared, so a change to how a server sends can be proven to send the same commands.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as cap from "./capture.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
export const ENUMS = path.join(here, "..", "..", "..", "src", "zandronum", "src", "network_enums.h");

// [rc4l] One capture line, "tic client r|u id ext size hex", as an object, or null for a line that is not one.
export function parseCaptureLine(line) {
  const m = /^(\d+) (\d+) ([ru]) (-?\d+) (-?\d+) (\d+)(?: ([0-9a-f]*))?$/.exec(String(line).trim());
  if (!m) return null;
  return { tic: +m[1], client: +m[2], reliable: m[3] === "r", id: +m[4], ext: +m[5], size: +m[6], hex: m[7] ?? "" };
}

export function parseCapture(text) {
  return String(text).split(/\r?\n/).map(parseCaptureLine).filter(Boolean);
}

// [rc4l] Each enum's names by value, read from network_enums.h: SVC from NUM_SERVERCONNECT_COMMANDS on, SVC2 from 0.
export function commandNames(source) {
  const names = {};
  const known = {};
  for (const block of String(source).matchAll(/BEGIN_ENUM\s*\(\s*(\w+)\s*\)([\s\S]*?)END_ENUM/g)) {
    const table = (names[block[1]] = {});
    let next = 0;
    for (const e of block[2].replace(/\/\/[^\n]*/g, "").matchAll(/ENUM_ELEMENT2?\s*\(\s*(\w+)\s*(?:,\s*(\w+)\s*)?\)/g)) {
      if (e[2] !== undefined) next = /^\d+$/.test(e[2]) ? Number(e[2]) : (known[e[2]] ?? 0);
      table[next] = e[1];
      known[e[1]] = next;
      next++;
    }
  }
  return names;
}

// [rc4l] A command's name: its SVC2 name for an extended command, else its SVC name, else its numbers.
export function commandName(names, id, ext) {
  const base = names.SVC?.[id] ?? `${id}`;
  if (ext < 0) return base;
  return names.SVC2?.[ext] ?? `${base}/${ext}`;
}

// [rc4l] How many of each command a capture holds, by name.
export function countByName(lines, names) {
  const counts = {};
  for (const l of lines) {
    const n = commandName(names, l.id, l.ext);
    counts[n] = (counts[n] || 0) + 1;
  }
  return counts;
}

// [rc4l] Each client's commands in order with tics counted from the capture's first, the form two runs of one fixture are compared in.
export function streams(lines) {
  const first = lines.length ? Math.min(...lines.map((l) => l.tic)) : 0;
  const out = {};
  for (const l of lines) (out[l.client] ||= []).push(`${l.tic - first} ${l.reliable ? "r" : "u"} ${l.id} ${l.ext} ${l.hex}`);
  return out;
}

// [rc4l] What differs between two captures: the commands whose counts changed, then the first line of each client's stream where they part.
export function diffCaptures(a, b, names) {
  const ca = countByName(a, names), cb = countByName(b, names);
  const counts = [...new Set([...Object.keys(ca), ...Object.keys(cb)])].sort()
    .filter((n) => (ca[n] || 0) !== (cb[n] || 0)).map((n) => ({ name: n, a: ca[n] || 0, b: cb[n] || 0 }));
  const sa = streams(a), sb = streams(b);
  const partings = [];
  for (const client of [...new Set([...Object.keys(sa), ...Object.keys(sb)])].sort()) {
    const x = sa[client] || [], y = sb[client] || [];
    const n = Math.max(x.length, y.length);
    for (let i = 0; i < n; i++) {
      if (x[i] !== y[i]) { partings.push({ client: Number(client), index: i, a: x[i] ?? null, b: y[i] ?? null }); break; }
    }
  }
  return { same: counts.length === 0 && partings.length === 0, counts, partings };
}

// [rc4l] A diff as lines for the terminal.
export function diffCaptureLines(d, names) {
  if (d.same) return ["SAME: every client was sent the same commands in the same order"];
  const name = (s) => { if (!s) return "(nothing)"; const [tic, r, id, ext] = s.split(" "); return `tic +${tic} ${r === "r" ? "reliable" : "unreliable"} ${commandName(names, +id, +ext)}`; };
  return ["DIFFER", ...d.counts.map((c) => `  ${c.name}: ${c.a} -> ${c.b}`), ...d.partings.map((p) => `  client ${p.client} parts at command ${p.index}: ${name(p.a)} vs ${name(p.b)}`)];
}

// [rc4l] Captures a server's sends for some seconds into file (the engine adds .netcap), returning the path written.
export async function recordCapture(c, file, { seconds = 5, exec = cap.exec, sleep = cap.sleep } = {}) {
  const target = /\.netcap$/i.test(file) ? file : `${file}.netcap`;
  const said = await exec(c, `fua_netcap_start "${path.resolve(target).replace(/\\/g, "/")}"`);
  if (!/capturing to/.test(said)) throw new Error(`the server did not start capturing: ${said.trim()}`);
  await sleep(seconds * 1000);
  await exec(c, "fua_netcap_stop");
  return target;
}

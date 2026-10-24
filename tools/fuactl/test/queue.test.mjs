import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { loadQueue, saveQueue, nextId, spotNameFor, addItem, noteItem, closeItem, nextOpen, formatItem, netQueueInfo } from "../src/queue.mjs";

const AT = { x: 1, y: 2, z: 3, angle: 90, pitch: 10 };

test("items are numbered in order, keep their ids once closed, and name their spot from the description", () => {
  let { queue, item } = addItem({ items: [] }, { description: "The ceiling is BLACK here!", map: "WDI03", at: AT }, "t0");
  assert.deepEqual(item, { id: 1, status: "open", description: "The ceiling is BLACK here!", spot: "q1-the-ceiling-is-black",
    map: "WDI03", at: AT, added: "t0", notes: [] });
  queue = closeItem(queue, 1, {}, "t1");
  ({ queue, item } = addItem(queue, { description: "mirror", spot: "my-spot", map: "WDI01", at: AT }, "t2"));
  assert.equal(item.id, 2);
  assert.equal(item.spot, "my-spot");
  assert.equal(nextId(queue), 3);
  assert.equal(spotNameFor(7, "  --  "), "q7");
});

test("the next item is the oldest still open", () => {
  let q = addItem({ items: [] }, { description: "a" }).queue;
  q = addItem(q, { description: "b" }).queue;
  assert.equal(nextOpen(q).description, "a");
  q = closeItem(q, 1, { unverified: true });
  assert.equal(nextOpen(q).description, "b");
  assert.equal(nextOpen(closeItem(q, 2)), null);
});

test("notes accumulate, a close can add one, and an unknown id is refused", () => {
  let q = addItem({ items: [] }, { description: "a" }).queue;
  q = noteItem(q, "1", "GL draws it, Vulkan does not", "t1");
  q = closeItem(q, 1, { unverified: true, note: "could not reproduce" }, "t2");
  assert.deepEqual(q.items[0].notes, [{ at: "t1", text: "GL draws it, Vulkan does not" }, { at: "t2", text: "could not reproduce" }]);
  assert.equal(q.items[0].status, "unverified");
  assert.equal(q.items[0].closed, "t2");
  assert.equal(closeItem(q, 1).items[0].notes.length, 2);
  assert.throws(() => noteItem(q, 9, "x"), /no queue item #9/);
});

test("a queue round-trips through its file, and a missing file is an empty queue", () => {
  const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-queue-")), "queue.json");
  assert.deepEqual(loadQueue(file), { items: [] });
  const q = addItem({ items: [] }, { description: "a", map: "MAP01", at: AT }, "t0").queue;
  saveQueue(q, file);
  assert.deepEqual(loadQueue(file), q);
  fs.rmSync(path.dirname(file), { recursive: true, force: true });
});

test("a listed item shows its id, status, map, spot and description", () => {
  const { item } = addItem({ items: [] }, { description: "sky is black", map: "WDI01" });
  assert.equal(formatItem(item), "#1 [open] WDI01 q1-sky-is-black -- sky is black");
  assert.equal(formatItem({ ...item, map: undefined }), "#1 [open] ? q1-sky-is-black -- sky is black");
});

test("a netgame report carries the instance's role and ping, and outside one nothing", () => {
  assert.deepEqual(netQueueInfo({ role: "client", consoleplayer: 1, ping: 48, serverTic: 900, connection: "active" }), { role: "client", player: 1, ping: 48, serverTic: 900, connection: "active" });
  assert.deepEqual(netQueueInfo({ role: "server", clients: [{ player: 0, ping: 30, state: "spawned", spectating: false, name: "x" }] }), { role: "server", clients: [{ player: 0, ping: 30, state: "spawned", spectating: false }] });
  assert.deepEqual(netQueueInfo({ role: "server" }).clients, []);
  assert.equal(netQueueInfo({ role: "single" }), null);
  assert.equal(netQueueInfo(null), null);
});

test("an item keeps its netgame side only when it has one", () => {
  const q = { items: [] };
  assert.deepEqual(addItem(q, { description: "x", map: "MAP01", at: null, net: { role: "client" } }, "now").item.net, { role: "client" });
  assert.equal("net" in addItem(q, { description: "x", map: "MAP01", at: null }, "now").item, false);
});

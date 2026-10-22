// [rc4l] The report queue: problems the user pointed at in a live session, each tied to a saved spot and worked oldest first (the report-queue skill).
import fs from "node:fs";
import { fileURLToPath } from "node:url";

export const QUEUE_FILE = fileURLToPath(new URL("../queue.json", import.meta.url));

// [rc4l] The queue on disk, or an empty one when there is none yet.
export function loadQueue(file = QUEUE_FILE) {
  return fs.existsSync(file) ? JSON.parse(fs.readFileSync(file, "utf8")) : { items: [] };
}

export function saveQueue(queue, file = QUEUE_FILE) {
  fs.writeFileSync(file, JSON.stringify(queue, null, 2) + "\n");
}

// [rc4l] The id the next item gets: one past the highest, so closed items keep theirs.
export function nextId(queue) {
  return queue.items.reduce((m, it) => Math.max(m, it.id), 0) + 1;
}

// [rc4l] A spot name from the item's id and the first words of its description, e.g. q3-ceiling-is-black-here.
export function spotNameFor(id, description) {
  const words = String(description).toLowerCase().split(/[^a-z0-9]+/).filter(Boolean).slice(0, 4);
  return [`q${id}`, ...words].join("-");
}

// [rc4l] The queue with a new open item at the end, and that item.
// [rc4l] A netgame instance's side of a report from its net.state: its role and ping, the server tic a client last heard of, a server's clients and their pings; null outside a netgame.
export function netQueueInfo(state) {
  if (!state || (state.role !== "client" && state.role !== "server")) return null;
  if (state.role === "client") return { role: "client", player: state.consoleplayer, ping: state.ping, serverTic: state.serverTic, connection: state.connection };
  return { role: "server", clients: (state.clients || []).map((c) => ({ player: c.player, ping: c.ping, state: c.state, spectating: c.spectating })) };
}

export function addItem(queue, { description, spot, map, at, net = null }, now = new Date().toISOString()) {
  const id = nextId(queue);
  const item = { id, status: "open", description, spot: spot || spotNameFor(id, description), map, at, ...(net ? { net } : {}), added: now, notes: [] };
  return { queue: { ...queue, items: [...queue.items, item] }, item };
}

function update(queue, id, change) {
  if (!queue.items.some((it) => it.id === Number(id))) throw new Error(`no queue item #${id}`);
  return { ...queue, items: queue.items.map((it) => (it.id === Number(id) ? change(it) : it)) };
}

// [rc4l] A finding written against an item, so an investigation survives the conversation that made it.
export function noteItem(queue, id, text, now = new Date().toISOString()) {
  return update(queue, id, (it) => ({ ...it, notes: [...it.notes, { at: now, text }] }));
}

// [rc4l] An item closed as done, or unverified when the fix went in without the fault ever reproducing.
export function closeItem(queue, id, { unverified = false, note = null } = {}, now = new Date().toISOString()) {
  return update(queue, id, (it) => ({ ...it, status: unverified ? "unverified" : "done", closed: now,
    notes: note ? [...it.notes, { at: now, text: note }] : it.notes }));
}

// [rc4l] The oldest item still open, or null.
export function nextOpen(queue) {
  return queue.items.find((it) => it.status === "open") || null;
}

// [rc4l] One line per item for `queue list`.
export function formatItem(it) {
  return `#${it.id} [${it.status}] ${it.map || "?"} ${it.spot} -- ${it.description}`;
}

// [rc4l] Two instances of one netgame compared: their synced actors by network id, their sectors, sidedefs and lines, the client's actors aligned to the server tic it last heard of, within a tolerance per field.
import * as cap from "./capture.mjs";

// [rc4l] How far apart two readings of one field may be and still agree.
export const DEFAULT_TOLERANCE = { pos: 1, angle: 2, health: 0, height: 0.5 };

// [rc4l] Every row of a paged world read (world.sectorstate, world.sidestate), page by page.
export async function readPaged(c, method, key, { page = 512 } = {}) {
  const rows = [];
  for (let first = 0; ; first += page) {
    const r = await c.rpc(method, { first, count: page });
    rows.push(...(r?.[key] || []));
    if (!r || first + page >= (r.total ?? 0)) return rows;
  }
}

// [rc4l] One instance's world: its net state, synced actors, sectors, sidedefs and lines (none from an engine older than world.linestate).
export async function readWorld(c, { sides = true } = {}) {
  const net = await c.rpc("net.state", {});
  const actors = await c.rpc("world.actors", { limit: 100000 });
  return { net, tic: actors?.tic ?? net?.gametic ?? 0, actors: actors?.actors || [], sectors: await readPaged(c, "world.sectorstate", "sectors"),
    sides: sides ? await readPaged(c, "world.sidestate", "sides") : [], lines: sides ? await readPaged(c, "world.linestate", "lines").catch(() => []) : [] };
}

// [rc4l] The sample of a server's history nearest to tic, never later than it when an earlier one exists, or null for no history.
export function sampleAt(history, tic) {
  const before = history.filter((s) => s.tic <= tic);
  if (before.length) return before.reduce((a, b) => (b.tic > a.tic ? b : a));
  return history.length ? history.reduce((a, b) => (b.tic < a.tic ? b : a)) : null;
}

const near = (a, b, tol) => Math.abs(Number(a) - Number(b)) <= tol;

// [rc4l] An angle's difference in degrees the short way round.
export function angleApart(a, b) {
  const d = Math.abs(Number(a) - Number(b)) % 360;
  return d > 180 ? 360 - d : d;
}

// [rc4l] An actor as a difference names it: its class, its network id and whether it is dead.
export const actorName = (a) => `${a.c} ${a.netid}${a.health <= 0 ? " (dead)" : ""}`;

// [rc4l] What differs between a server's actors and a client's, matched by network id: actors one side lacks, then each field past its tolerance; skip names ids (a predicted pawn) whose movement is not compared.
export function diffActors(server, client, { tolerance = DEFAULT_TOLERANCE, skip = new Set(), positions = true } = {}) {
  const out = [];
  const byId = (list) => new Map(list.map((a) => [a.netid, a]));
  const s = byId(server), k = byId(client);
  for (const [id, a] of s) if (!k.has(id)) out.push({ kind: "actor", netid: id, what: `${actorName(a)} only on the server` });
  for (const [id, b] of k) if (!s.has(id)) out.push({ kind: "actor", netid: id, what: `${actorName(b)} only on the client` });
  for (const [id, a] of s) {
    const b = k.get(id);
    if (!b) continue;
    if (a.c !== b.c) out.push({ kind: "actor", netid: id, what: `class ${a.c} on the server, ${b.c} on the client (${id})` });
    if (a.health !== b.health && Math.abs(a.health - b.health) > tolerance.health) out.push({ kind: "actor", netid: id, what: `${actorName(a)} health ${a.health} on the server, ${b.health} on the client` });
    if (!positions || skip.has(id)) continue;
    const apart = Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    if (apart > tolerance.pos) out.push({ kind: "actor", netid: id, what: `${actorName(a)} ${apart.toFixed(2)} units apart`, apart });
    if (angleApart(a.angle, b.angle) > tolerance.angle) out.push({ kind: "actor", netid: id, what: `${actorName(a)} facing ${a.angle} on the server, ${b.angle} on the client` });
  }
  return out;
}

// [rc4l] What differs between two instances' sectors and sidedefs, by index.
export function diffMap(server, client, { tolerance = DEFAULT_TOLERANCE } = {}) {
  const out = [];
  if (server.sectors.length !== client.sectors.length) out.push({ kind: "map", what: `${server.sectors.length} sectors on the server, ${client.sectors.length} on the client` });
  const n = Math.min(server.sectors.length, client.sectors.length);
  for (let i = 0; i < n; i++) {
    const a = server.sectors[i], b = client.sectors[i];
    const fields = [];
    if (!near(a.floor, b.floor, tolerance.height)) fields.push(`floor ${a.floor}/${b.floor}`);
    if (!near(a.ceiling, b.ceiling, tolerance.height)) fields.push(`ceiling ${a.ceiling}/${b.ceiling}`);
    for (const f of ["light", "color", "fade", "desat", "floorTex", "ceilingTex", "gravity", "friction", "special"]) if (a[f] !== b[f]) fields.push(`${f} ${a[f]}/${b[f]}`);
    for (const f of ["reflect", "scale", "rotation"]) if (String(a[f]) !== String(b[f])) fields.push(`${f} ${a[f]}/${b[f]}`);
    if (fields.length) out.push({ kind: "sector", i: a.i, what: `sector ${a.i}: ${fields.join(", ")} (server/client)` });
  }
  const m = Math.min(server.sides.length, client.sides.length);
  for (let i = 0; i < m; i++) {
    const a = server.sides[i], b = client.sides[i];
    const fields = ["top", "mid", "bottom", "light", "flags"].filter((f) => a[f] !== b[f]).map((f) => `${f} ${a[f]}/${b[f]}`);
    if (fields.length) out.push({ kind: "side", i: a.i, what: `side ${a.i}: ${fields.join(", ")} (server/client)` });
  }
  const sl = server.lines || [], cl = client.lines || [];
  for (let i = 0; i < Math.min(sl.length, cl.length); i++) {
    const a = sl[i], b = cl[i];
    const fields = ["alpha", "flags"].filter((f) => a[f] !== b[f]).map((f) => `${f} ${a[f]}/${b[f]}`);
    if (fields.length) out.push({ kind: "line", i: a.i, what: `line ${a.i}: ${fields.join(", ")} (server/client)` });
  }
  return out;
}

// [rc4l] A server's actors recorded each tic for a while, so a client's can be set against the tic it last heard of.
export async function recordServerHistory(sc, { seconds = 2, sleep = cap.sleep, now = () => Date.now() } = {}) {
  const history = [];
  const until = now() + seconds * 1000;
  let last = -1;
  while (now() < until) {
    const r = await sc.rpc("world.actors", { limit: 100000 });
    if (r && r.tic !== last) { history.push({ tic: r.tic, actors: r.actors || [] }); last = r.tic; }
    await sleep(5);
  }
  return history;
}

// [rc4l] A netdiff verdict as lines: agreed, or each difference.
export function diffLines(label, diffs, compared) {
  if (!diffs.length) return [`AGREE ${label}: ${compared.actors} actors, ${compared.sectors} sectors, ${compared.sides} sides${compared.lines ? `, ${compared.lines} lines` : ""}`];
  return [`DIFFER ${label}: ${diffs.length} difference(s)`, ...diffs.slice(0, 40).map((d) => `  ${d.what}`), ...(diffs.length > 40 ? [`  ... ${diffs.length - 40} more`] : [])];
}

// [rc4l] The server against one client: the map once (it should have settled), the actors against the server's history at the client's last heard tic, the client's own predicted pawn left out of the positions.
export async function netdiff(sc, cc, { tolerance = DEFAULT_TOLERANCE, seconds = 2, deps = {} } = {}) {
  const { record = recordServerHistory, read = readWorld } = deps;
  const history = await record(sc, { seconds });
  const server = await read(sc);
  const client = await read(cc);
  history.push({ tic: server.tic, actors: server.actors });
  const aligned = sampleAt(history, client.net?.serverTic ?? server.tic) || { tic: server.tic, actors: server.actors };
  const own = client.actors.filter((a) => a.player === client.net?.consoleplayer).map((a) => a.netid);
  const diffs = [...diffMap(server, client, { tolerance }), ...diffActors(aligned.actors, client.actors, { tolerance, skip: new Set(own) })];
  return { diffs, compared: { actors: client.actors.length, sectors: client.sectors.length, sides: client.sides.length, lines: (client.lines || []).length }, alignedTic: aligned.tic, clientTic: client.net?.serverTic };
}

// [rc4l] Each client's netdiff against its server as PASS or FAIL lines named by its role and player, and how many failed.
export async function clientAgreement(sc, clients, label, diff = netdiff) {
  const lines = [];
  let failed = 0;
  for (const c of clients) {
    const r = await diff(sc, c.c, { seconds: 1 });
    lines.push(...diffLines(`${label} ${c.role} player ${c.player}`, r.diffs, r.compared).map((l) => l.replace(/^AGREE/, "PASS").replace(/^DIFFER/, "FAIL")));
    if (r.diffs.length) failed++;
  }
  return { lines, failed };
}

// [rc4l] A hosted game taken from MAP01 to MAP02 by changemap (through intermission), every client reconnecting there on its own, and on to MAP03 by map (the server sending every client back to reconnect), a player and an observer aboard: exit 0 when after each step both clients saw what they should (the intermission, the new map), are connected and playing as the same account the server holds them as, hold the world as the server does, and see the player's shots kill a zombieman, on loopback and at 100 ms.
//
// usage:  node scenarios/netmapchange.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";
import { clientPlaying, fireUntilDead, joinGame } from "../src/netplay.mjs";
import { clientAgreement } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";

// [rc4l] Each map lit its own way, so a client on the wrong one differs at once.
export const MAPS = [{ name: "MAP01", light: 160 }, { name: "MAP02", light: 176 }, { name: "MAP03", light: 208 }];

// [rc4l] Three closed rooms, one per start since a reconnect can hand the player any of them, so a shot wakes only its own room: two zombiemen ahead of the start for the step that arrives, and two behind it, past two sound-blocking lines so they sleep until the step that stays on the map turns round to them.
export const ROWS = [512, 160, 864];

export function buildNetMapChangeWad() {
  return wad(MAPS.flatMap(({ name, light }) => {
    const m = new UdmfMap();
    const floor = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: light };
    const quiet = () => ({ blocksound: true });
    ROWS.forEach((y, i) => {
      const room = m.room(0, y - 160, 1024, y + 160, floor, () => ({ side: { texturemiddle: "STARTAN2" } }));
      const nook = m.block(8, y - 152, 304, y + 152, floor, room, () => ({}), () => ({}), quiet);
      m.block(16, y - 144, 288, y + 144, floor, nook, () => ({}), () => ({}), quiet);
      m.thing({ x: 512, y, angle: 0, type: i + 1 });
      for (const x of [832, 960]) m.thing({ x, y, angle: 0, type: THING.Zombieman, ambush: true });
      for (const x of [208, 96]) m.thing({ x, y, angle: 180, type: THING.Zombieman, ambush: true });
    });
    return mapLumps(name, m);
  }));
}

// [rc4l] Whether an actor is in the same room as a pawn and ahead of where it faces.
export function aheadOf(pawn, a) {
  const r = (pawn.angle * Math.PI) / 180;
  return Math.abs(a.y - pawn.y) < 160 && (a.x - pawn.x) * Math.cos(r) + (a.y - pawn.y) * Math.sin(r) > 0;
}

// [rc4l] The faults of one step from what was seen: per client whether it passed the intermission, reached the map, came back playing and as which account and holds the zombieman dead, then whether the server's died.
export function judgeChange(step, { clients, zombieDied, agreed }) {
  const faults = [];
  for (const c of clients) {
    if (step.intermission && !c.sawIntermission) faults.push(`${c.who} never showed the intermission`);
    if (c.map !== step.map) faults.push(`${c.who} is on ${c.map || "no map"}, not ${step.map}`);
    if (c.connection !== "active") faults.push(`${c.who}'s connection is ${c.connection || "gone"}`);
    if (!c.playing) faults.push(`${c.who} came back ${c.spectating ? "spectating" : "not in the game"}`);
    if (!c.account) faults.push(`${c.who} has no account`);
    else if (c.account !== c.accountBefore) faults.push(`${c.who} plays as ${c.account}, not ${c.accountBefore} as before`);
    if (c.account && c.account !== c.serverAccount) faults.push(`the server holds ${c.who} as ${c.serverAccount}, the client says ${c.account}`);
    if (zombieDied && !(c.zombieHealth <= 0)) faults.push(`${c.who} holds the zombieman at ${c.zombieHealth}`);
  }
  if (!zombieDied) faults.push("the player's shots never killed a zombieman on the server");
  // [rc4l] Each client that differed and what it held otherwise, the first eight lines.
  return faults.concat(agreed.failed ? agreed.lines.filter((l) => !l.startsWith("PASS")).slice(0, 8).map((l) => l.trim()) : []);
}

const ticOf = async (c) => (await c.rpc("sim.tic").catch(() => ({}))) || {};

// [rc4l] Until a client is connected and in a level on map: whether it showed the intermission on the way, pressing use through one as a player does.
async function arriveOn(c, map, timeoutMs) {
  let sawIntermission = false;
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const t = await ticOf(c);
    if (t.gamestate === cap.GS_INTERMISSION) {
      sawIntermission = true;
      await cap.exec(c, "+use", { quietMs: 60 });
      await cap.exec(c, "-use", { quietMs: 60 });
    } else if (t.inlevel && t.map === map && (await c.rpc("net.state", {}))?.connection === "active") return { sawIntermission, map: t.map };
    await cap.sleep(150);
  }
  return { sawIntermission, map: (await ticOf(c)).map };
}

// [rc4l] Each client's account as the server holds it and as the client says, by player.
async function accountsOf(set) {
  const server = new Map((((await set.server.c.rpc("net.state", {}))?.clients) || []).map((r) => [r.player, r.account]));
  const out = [];
  for (const c of set.clients) {
    const mine = await c.c.rpc("net.state", {});
    out.push({ who: `${c.role} player ${mine?.consoleplayer}`, role: c.role, player: mine?.consoleplayer, account: mine?.account || "",
      connection: mine?.connection, serverAccount: server.get(mine?.consoleplayer) || "" });
  }
  return out;
}

// [rc4l] What each step says of itself when it passes.
const PASSED = {
  intermission: (s) => `reached ${s.map} through the intermission`,
  clients: () => "reconnected on their own and joined again",
  server: (s) => `were sent to ${s.map} to reconnect and came back playing`,
};

async function change(set, label, step, before) {
  const sc = set.server.c;
  if (step.by === "clients") for (const c of set.clients) await cap.exec(c.c, step.command, cap.SLOW_COMMAND);
  else await cap.exec(sc, step.command, cap.SLOW_COMMAND);
  const arrived = await Promise.all(set.clients.map((c) => arriveOn(c.c, step.map, scaled(60000))));
  // [rc4l] A client that reconnects of its own accord arrives a spectator and asks to play, as a player does.
  if (step.by === "clients") for (const c of set.clients) await joinGame(c, sc);
  await cap.waitInLevel(sc, scaled(30000));
  await cap.waitTics(sc, 70);
  const serverState = await sc.rpc("net.state", {});
  const now = await accountsOf(set);
  for (const c of set.clients) await cap.exec(c.c, "god", { quietMs: 120 });
  const shooter = set.clients.find((c) => c.role === "player");
  const pawnOf = async () => ((await sc.rpc("world.actors", {}))?.actors || []).find((a) => a.player === now[set.clients.indexOf(shooter)].player);
  let pawn = await pawnOf();
  const ahead = (a) => /zombie/i.test(a.c) && a.health > 0 && !!pawn && aheadOf(pawn, a);
  // [rc4l] Back in a room whose zombiemen ahead died, the player turns round to the two asleep behind.
  if (!((await sc.rpc("world.actors", {}))?.actors || []).some(ahead)) {
    await cap.exec(shooter.c, "turn180", { quietMs: 60 });
    await cap.waitTics(sc, 20);
    pawn = await pawnOf();
  }
  // [rc4l] Both ahead shot dead (a stray shot wakes the one behind), so the world compared has stopped moving.
  const kills = [];
  for (let id; kills.length < 4 && (id = await fireUntilDead(shooter.c, sc, ahead)) !== null;) kills.push(id);
  await cap.waitTics(sc, 35);
  const clients = [];
  for (let i = 0; i < set.clients.length; i++) {
    set.clients[i].player = now[i].player;
    const held = new Map(((await set.clients[i].c.rpc("world.actors", {}))?.actors || []).map((a) => [a.netid, a.health]));
    const row = (serverState?.clients || []).find((r) => r.player === now[i].player);
    clients.push({ ...now[i], ...arrived[i], accountBefore: before[i].account, playing: clientPlaying(serverState, now[i].player), spectating: !!row?.spectating,
      zombieHealth: kills.reduce((most, id) => (held.has(id) && most !== undefined ? Math.max(most, held.get(id)) : undefined), -Infinity) });
  }
  const agreed = await clientAgreement(sc, set.clients, `${label} ${step.map}`);
  const faults = judgeChange(step, { clients, zombieDied: kills.length > 0, agreed });
  return faults.length ? { line: `FAIL ${label} ${step.command}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label} ${step.command}: ${clients.length} clients ${PASSED[step.intermission ? "intermission" : step.by](step)}, playing as the same accounts, the world agreed and the zombiemen shot died on all`, failed: 0 };
}

export const STEPS = [
  { command: "changemap MAP02", map: "MAP02", intermission: true, by: "server" },
  { command: "reconnect", map: "MAP02", intermission: false, by: "clients" },
  { command: "map MAP03", map: "MAP03", intermission: false, by: "server" },
];

export async function judgeNetplay(set, label) {
  const before = await accountsOf(set);
  const lines = [];
  let failed = 0;
  if (before.some((c) => !c.account || c.account !== c.serverAccount)) { lines.push(`FAIL ${label}: the accounts at the start disagree: ${JSON.stringify(before)}`); failed++; }
  for (const step of STEPS) {
    const r = await change(set, label, step, before);
    lines.push(r.line);
    failed += r.failed;
  }
  return { lines, failed };
}

// [rc4l] On loopback and at 100 ms of ping for both clients.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netmapchange", wad: buildNetMapChangeWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);

// [Doruk-Aksoy] A hosted map where a player uses a one-time switch that lowers a floor and crosses one-time lines that open a door and raise a platform, then reconnects, then a third player joins late: exit 0 when the server's world changed and the player (before and after its reconnect), an observer and the late joiner all hold the moved planes and the pressed switch as the server does.
//
// usage:  node scenarios/netonetime.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareNetplay } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { addClient, joinGame } from "../src/netplay.mjs";
import { clientAgreement, readWorld } from "../src/netdiff.mjs";
import * as cap from "../src/capture.mjs";
import { scaled } from "../src/timeouts.mjs";
import { captureVulkan } from "../src/shot.mjs";
import { decodePNG } from "../src/pngcore.mjs";
import os from "node:os";
import path from "node:path";

const LOWER_TAG = 10, DOOR_TAG = 11, PLAT_TAG = 12;
const flats = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const plain = { side: { texturemiddle: "STARTAN2" } };
const step = () => ({ texturebottom: "STARTAN2", texturetop: "STARTAN2" });

// [Doruk-Aksoy] The sectors in the order the map makes them, after the room: the raised floor the switch lowers, the door the first line opens, the platform the second raises.
export const SECTOR = { lower: 1, door: 2, plat: 3 };
// [Doruk-Aksoy] What each one-time action leaves, which every client must come to hold.
export const AFTER = { lowerFloor: 0, doorCeilingAbove: 0, platFloor: 48, switchTex: "SW2STRTN" };

// [Doruk-Aksoy] Player 1 faces a one-time switch on the west wall with two one-time lines behind it; the others start far east, clear of the lines.
export function buildNetOneTimeWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 768, flats, (edge) => (edge === "west"
    ? { side: { texturemiddle: "SW1STRTN" }, line: { special: SPECIAL.Floor_LowerByValue, arg0: LOWER_TAG, arg1: 32, arg2: 64, playeruse: true } }
    : plain));
  m.block(300, 560, 420, 700, { ...flats, heightfloor: 64, id: LOWER_TAG }, room, step);
  m.block(448, 40, 576, 72, { ...flats, heightceiling: 0, id: DOOR_TAG }, room, () => ({ texturemiddle: "-", texturetop: "BIGDOOR2" }));
  m.block(700, 100, 860, 260, { ...flats, id: PLAT_TAG }, room, step);
  m.opening(120, 300, 120, 470, room, room, {}, {}, { special: SPECIAL.Door_Open, arg0: DOOR_TAG, arg1: 64, playercross: true });
  m.opening(170, 300, 170, 470, room, room, {}, {}, { special: SPECIAL.Floor_RaiseByValue, arg0: PLAT_TAG, arg1: 32, arg2: 48, playercross: true });
  m.thing({ x: 40, y: 384, angle: 180, type: 1 });
  m.thing({ x: 960, y: 600, angle: 180, type: 2 });
  m.thing({ x: 960, y: 680, angle: 180, type: 3 });
  m.thing({ x: 960, y: 520, angle: 180, type: 4 });
  return wad(mapLumps("MAP01", m));
}

// [Doruk-Aksoy] What a world holds of the three actions, wrong ones named: each plane where the action leaves it and the switch pressed.
export function oneTimeFaults({ sectors, sides }) {
  const faults = [];
  const at = (i) => sectors.find((s) => s.i === i);
  if (at(SECTOR.lower)?.floor !== AFTER.lowerFloor) faults.push(`the switch's floor is at ${at(SECTOR.lower)?.floor}, not ${AFTER.lowerFloor}`);
  if (!(at(SECTOR.door)?.ceiling > AFTER.doorCeilingAbove)) faults.push(`the door's ceiling is at ${at(SECTOR.door)?.ceiling}`);
  if (at(SECTOR.plat)?.floor !== AFTER.platFloor) faults.push(`the platform's floor is at ${at(SECTOR.plat)?.floor}, not ${AFTER.platFloor}`);
  if (!sides.some((s) => s.mid === AFTER.switchTex)) faults.push("the switch shows unpressed");
  return faults;
}

// [Doruk-Aksoy] Each client's faults by role, as lines.
async function judgeClients(set, label, stage) {
  const lines = [];
  let failed = 0;
  for (const c of set.clients) {
    const faults = oneTimeFaults(await readWorld(c.c));
    if (faults.length) failed++;
    lines.push(`${faults.length ? "FAIL" : "PASS"} ${label} ${stage}: ${c.role}${faults.length ? `: ${faults.join("; ")}` : " holds all three actions"}`);
  }
  return { lines, failed };
}

// [Doruk-Aksoy] The player presses the switch, then backs east over both lines, and the server is left to finish the moves.
export async function actOnce(sc, player) {
  await cap.exec(player.c, "+use", { quietMs: 120 });
  await cap.waitTics(sc, 4);
  await cap.exec(player.c, "-use", { quietMs: 60 });
  await cap.exec(player.c, "+back", { quietMs: 60 });
  await cap.waitTics(sc, 30);
  await cap.exec(player.c, "-back", { quietMs: 60 });
  await cap.waitTics(sc, 35 * 4);
}

// [Doruk-Aksoy] The player reconnects as a player does and asks to play again; false when it never came back.
// [Doruk-Aksoy] With gapMs, it disconnects, stays away that long with the server empty, then connects by address, as a player who left and came back does.
async function reconnect(sc, player, { gapMs = 0, gamePort = 0 } = {}) {
  if (gapMs > 0) {
    await cap.exec(player.c, "disconnect", cap.SLOW_COMMAND);
    await cap.sleep(gapMs);
    await cap.exec(player.c, `connect 127.0.0.1:${gamePort}`, cap.SLOW_COMMAND);
  } else await cap.exec(player.c, "reconnect", cap.SLOW_COMMAND);
  const deadline = Date.now() + scaled(60000);
  for (;;) {
    const t = (await player.c.rpc("sim.tic").catch(() => ({}))) || {};
    if (t.inlevel && (await player.c.rpc("net.state", {}).catch(() => ({})))?.connection === "active") break;
    if (Date.now() > deadline) return false;
    await cap.sleep(200);
  }
  await joinGame(player, sc);
  return true;
}

// [Doruk-Aksoy] Survival seconds of countdown, short so a round starts soon after a player arrives.
export const SURVIVAL_COUNTDOWN = 5;

// [Doruk-Aksoy] Survival: the round started, the actions taken, the player reconnecting; the server and every client named as holding the actions or the map's start, during the new countdown and after its FIGHT, and each client judged against the server at both.
async function judgeSurvival(set, label) {
  const sc = set.server.c;
  const player = set.clients.find((c) => c.role === "player");
  const lines = [];
  let failed = 0;
  const held = (w) => (oneTimeFaults(w).length === 0 ? "the actions" : oneTimeFaults(w).length === 4 ? "the map's start" : `part: ${oneTimeFaults(w).join("; ")}`);
  const stage = async (name) => {
    lines.push(`INFO ${label} ${name}: server holds ${held(await readWorld(sc))}; ${(await Promise.all(set.clients.map(async (c) => `${c.role} ${held(await readWorld(c.c))}`))).join(", ")}`);
    const agreed = await clientAgreement(sc, set.clients, `${label} ${name}`);
    lines.push(...agreed.lines.filter((l) => !l.startsWith("PASS")));
    failed += agreed.failed;
  };
  await cap.waitTics(sc, 35 * (SURVIVAL_COUNTDOWN + 3));
  await actOnce(sc, player);
  const serverFaults = oneTimeFaults(await readWorld(sc));
  if (serverFaults.length) return { lines: [`FAIL ${label}: the server's world did not change: ${serverFaults.join("; ")}`], failed: 1 };
  if (!(await reconnect(sc, player))) return { lines: [`FAIL ${label}: the player never came back from reconnect`], failed: 1 };
  await cap.waitTics(sc, 35);
  await stage("during the countdown after the reconnect");
  await cap.waitTics(sc, 35 * (SURVIVAL_COUNTDOWN + 3));
  await stage("after the round's FIGHT");
  return { lines, failed };
}

// [Doruk-Aksoy] Where the player stands to look at what the actions moved, and the angles it looks from there: the lowered floor north-west, the door south, the platform east.
export const LOOK_FROM = { x: 512, y: 420 };
export const LOOK_ANGLES = [125, 270, 330];
// [Doruk-Aksoy] The worst 16-pixel block a picture of the same world from the same spot may differ by (the weapon's idle bob, the HUD).
export const PICTURE_CEILING = 6;

// [Doruk-Aksoy] The player's pictures of the moved sectors from LOOK_FROM, one an angle, written under prefix; the files.
export async function lookAtActions(sc, player, prefix) {
  // [Doruk-Aksoy] No notices on screen, since each capture prints its file name.
  await cap.exec(player.c, "show_messages 0", { quietMs: 100 });
  await cap.exec(player.c, `warp ${LOOK_FROM.x} ${LOOK_FROM.y}`, { quietMs: 200 });
  await cap.waitTics(sc, 8);
  const files = [];
  for (const angle of LOOK_ANGLES) {
    const now = (await player.c.rpc("input.look", { yaw: 0 }))?.angle_deg ?? 0;
    await player.c.rpc("input.look", { yaw: ((angle - now + 540) % 360) - 180 });
    await cap.waitTics(sc, 6);
    const file = `${prefix}-${angle}.png`;
    files.push(await captureVulkan(player.c, file));
  }
  return files;
}

// [Doruk-Aksoy] Each angle's worst block between two sets of pictures, as lines, failing past PICTURE_CEILING.
function judgePictures(label, who, before, after) {
  const lines = [];
  let failed = 0;
  LOOK_ANGLES.forEach((angle, i) => {
    if (!before[i] || !after[i]) { lines.push(`FAIL ${label} picture ${angle}: no capture`); failed++; return; }
    const worst = worstBlock(decodePNG(before[i]), decodePNG(after[i]));
    const bad = worst.d > PICTURE_CEILING;
    if (bad) failed++;
    lines.push(`${bad ? "FAIL" : "PASS"} ${label} picture ${angle}: the ${who} draws the moved sectors ${bad ? "otherwise than" : "as"} the player did before it left (worst block ${worst.d.toFixed(1)} at ${worst.x},${worst.y}; ${before[i]} / ${after[i]})`);
  });
  return { lines, failed };
}

// [Doruk-Aksoy] Rows a picture is judged from: above them a joiner shows its teammate's name, health and weapon, which the player alone never did, and nothing moved draws there.
export const PICTURE_FROM_Y = 160;

// [Doruk-Aksoy] Two decoded frames' worst 16-pixel block by mean |d| per pixel and channel, from row fromY down, and where.
export function worstBlock(a, b, block = 16, fromY = PICTURE_FROM_Y) {
  let worst = { d: 0, x: 0, y: 0 };
  for (let by = fromY; by < a.h; by += block) for (let bx = 0; bx < a.w; bx += block) {
    let s = 0, k = 0;
    for (let y = by; y < Math.min(by + block, a.h); y++) for (let x = bx; x < Math.min(bx + block, a.w); x++)
      for (let ch = 0; ch < 3; ch++) { const i = (y * a.w + x) * a.channels + ch; s += Math.abs(a.data[i] - b.data[(y * b.w + x) * b.channels + ch]); k++; }
    if (s / k > worst.d) worst = { d: s / k, x: bx, y: by };
  }
  return worst;
}

export async function judgeNetplay(set, label, { config } = {}) {
  if (config?.survival) return judgeSurvival(set, label);
  const sc = set.server.c;
  const player = set.clients.find((c) => c.role === "player");
  const lines = [];
  let failed = 0;
  const add = (r) => { lines.push(...r.lines); failed += r.failed; };
  await cap.waitTics(sc, 35);
  await actOnce(sc, player);
  const serverFaults = oneTimeFaults(await readWorld(sc));
  if (serverFaults.length) return { lines: [`FAIL ${label}: the server's world did not change: ${serverFaults.join("; ")}`], failed: 1 };
  add(await judgeClients(set, label, "before the reconnect"));
  // [Doruk-Aksoy] The state read says what the game holds; the pictures say what the player is shown, which a stale mesh would leave at the map's start.
  const shots = path.join(os.tmpdir(), `netonetime-${label.replace(/\W+/g, "-")}`);
  const before = await lookAtActions(sc, player, `${shots}-before`);
  if (!(await reconnect(sc, player, { gapMs: config?.gapMs || 0, gamePort: set.gamePort }))) return { lines: [...lines, `FAIL ${label}: the player never came back from reconnect`], failed: failed + 1 };
  await cap.waitTics(sc, 35 * 2);
  add(await judgeClients(set, label, "after the reconnect"));
  // [Doruk-Aksoy] afterCommands, run on the reconnected player before its pictures, test what would put its drawing right.
  for (const command of config?.afterCommands || []) lines.push(`INFO ${label} ${command}: ${(await cap.exec(player.c, command, cap.SLOW_COMMAND)).trim()}`);
  if (config?.afterCommands) await cap.waitTics(sc, 35);
  add(judgePictures(label, "reconnected player", before, await lookAtActions(sc, player, `${shots}-after`)));
  await addClient(set, { role: "latejoin" });
  await cap.waitTics(sc, 35 * 2);
  add(await judgeClients(set, label, "after a late join"));
  const late = set.clients.find((c) => c.role === "latejoin");
  // [Doruk-Aksoy] Playing, so it sees from a player's height as the player did.
  if (late) {
    const joined = await joinGame(late, sc);
    if (!joined.joined) { lines.push(`FAIL ${label}: the late joiner never joined the game (${JSON.stringify(joined.mine)})`); failed++; }
    // [Doruk-Aksoy] The player steps off the spot first, or the joiner lands on its head and looks from 56 higher.
    await cap.exec(player.c, "warp 900 650", { quietMs: 200 });
    await cap.waitTics(sc, 35);
  }
  if (late) add(judgePictures(label, "late joiner", before, await lookAtActions(sc, late, `${shots}-late`)));
  const agreed = await clientAgreement(sc, set.clients, label);
  return { lines: [...lines, ...agreed.lines], failed: failed + agreed.failed };
}

// [Doruk-Aksoy] On loopback and at 100 ms of ping.
export const CONFIGS = [
  { name: "lan", players: 1, observers: 1 },
  { name: "ping100", players: 1, observers: 1, lag: "100" },
  // [Doruk-Aksoy] Alone, so the server empties while the player is away, as a host testing by themselves has it.
  { name: "coop-alone", players: 1 },
  { name: "coop-alone-away", players: 1, gapMs: 15000 },
  { name: "survival-alone", players: 1, survival: true, serverCvars: { survival: 1, sv_survivalcountdowntime: SURVIVAL_COUNTDOWN } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "netonetime", wad: buildNetOneTimeWad(), maps: ["MAP01"], configs: CONFIGS, judgeNetplay }), compareNetplay);
